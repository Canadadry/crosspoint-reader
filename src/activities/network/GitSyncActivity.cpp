#include "GitSyncActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <string>
#include <utility>
#include <vector>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/GitSyncConfigLoader.h"

namespace {

// Volatile stores so the wipe of the token is not dropped as a dead store.
void wipe(void* data, const size_t len) {
  volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

constexpr int GIT_SYNC_DETAIL_MAX_LINES = 2;

}  // namespace

void GitSyncActivity::onEnter() {
  Activity::onEnter();

  // The loader logs its verdict; Wi-Fi stays off on any error so onExit does not restart.
  if (git_sync_config::load(config).status != GitSyncConfigStatus::Ok) {
    setState(State::ConfigError);
    requestUpdate();
    return;
  }

  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    setState(State::Starting);
    requestUpdate();
    return;
  }
  launchWifiSelection();
}

void GitSyncActivity::onExit() {
  Activity::onExit();
  wipeConfig();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void GitSyncActivity::launchWifiSelection() {
  setState(State::WifiSelection);
  requestUpdate();

  auto wifiSelection = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifiSelection) {
    LOG_ERR("GSYNC", "OOM: Wi-Fi selection activity");
    setState(State::WifiFailed);
    return;
  }
  startActivityForResult(std::move(wifiSelection),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void GitSyncActivity::onWifiSelectionComplete(const bool connected) {
  // Wi-Fi is left as is on failure; onExit's silent restart tears it down.
  // The run itself starts from loop(), once the Wi-Fi selection activity is destroyed.
  setState(connected ? State::Starting : State::WifiFailed);
}

void GitSyncActivity::loop() {
  switch (state) {
    case State::Starting:
      runSync();
      return;
    case State::ConfigError:
    case State::WifiFailed:
    case State::Done:
      if (endScreenDismissed()) {
        onGoHome();
      }
      return;
    case State::WifiSelection:
    case State::Running:
      return;
  }
}

void GitSyncActivity::runSync() {
  wipeConfig();
  setState(State::Done);
  requestUpdate();
}

void GitSyncActivity::wipeConfig() { wipe(&config, sizeof(config)); }

void GitSyncActivity::setState(const State next) {
  RenderLock lock(*this);
  state = next;
}

bool GitSyncActivity::endScreenDismissed() const {
  int x = 0;
  int y = 0;
  return mappedInput.wasReleased(MappedInputManager::Button::Back) ||
         mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y);
}

void GitSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 tr(STR_GIT_SYNC_TITLE));

  const int top = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  LineStack lines{Rect{safe.x + metrics.contentSidePadding, top, safe.width - metrics.contentSidePadding * 2,
                       safe.y + safe.height - top},
                  top};

  switch (state) {
    case State::WifiSelection:
      renderer.displayBuffer();
      return;
    case State::Starting:
    case State::Running:
      renderProgress(lines);
      renderer.displayBuffer();
      return;
    case State::ConfigError:
      drawLine(lines, tr(STR_GIT_SYNC_CONFIG_INVALID), EpdFontFamily::BOLD);
      drawWrapped(lines, tr(STR_GIT_SYNC_CONFIG_HINT), GIT_SYNC_DETAIL_MAX_LINES);
      break;
    case State::WifiFailed:
      drawLine(lines, tr(STR_WIFI_CONN_FAILED), EpdFontFamily::BOLD);
      break;
    case State::Done:
      renderDone(lines);
      break;
  }

  // End screens: both buttons leave, and a half refresh clears the ghosting of the progress redraws.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OK_BUTTON), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void GitSyncActivity::renderProgress(LineStack& lines) const {
  drawLine(lines, tr(STR_GIT_SYNC_PREPARING), EpdFontFamily::BOLD);
}

void GitSyncActivity::renderDone(LineStack&) const {}

bool GitSyncActivity::drawLine(LineStack& lines, const char* text, const EpdFontFamily::Style style) const {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  if (lines.y + lineHeight > lines.area.y + lines.area.height) {
    return false;
  }
  const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, text, lines.area.width, style);
  UITheme::drawCenteredText(renderer, lines.area, UI_10_FONT_ID, lines.y, fitted.c_str(), true, style);
  lines.y += lineHeight + UITheme::getInstance().getMetrics().verticalSpacing / 2;
  return true;
}

void GitSyncActivity::drawWrapped(LineStack& lines, const char* text, const int maxLines) const {
  const std::vector<std::string> wrapped = renderer.wrappedText(UI_10_FONT_ID, text, lines.area.width, maxLines);
  for (const auto& line : wrapped) {
    if (!drawLine(lines, line.c_str())) {
      return;
    }
  }
}
