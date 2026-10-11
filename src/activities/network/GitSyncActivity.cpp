#include "GitSyncActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/GitSyncConfigLoader.h"
#include "network/GitSyncEngine.h"
#include "util/TaskWatchdog.h"

namespace {

// Volatile stores so the wipe of the token is not dropped as a dead store.
void wipe(void* data, const size_t len) {
  volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

constexpr int GIT_SYNC_DETAIL_MAX_LINES = 2;
constexpr size_t GIT_SYNC_LINE_BYTES = 64;
// Wider than any content width can show, so truncatedText cuts the line before an snprintf cut would show.
constexpr size_t GIT_SYNC_PROGRESS_LINE_BYTES = 192;

const char* opKindLabel(const SyncOpKind kind) {
  switch (kind) {
    case SyncOpKind::Move:
      return tr(STR_GIT_SYNC_STEP_MOVE);
    case SyncOpKind::AdoptOrDownload:
      return tr(STR_GIT_SYNC_STEP_CHECK);
    case SyncOpKind::Download:
      return tr(STR_GIT_SYNC_STEP_DOWNLOAD);
    case SyncOpKind::Delete:
      return tr(STR_GIT_SYNC_STEP_DELETE);
    case SyncOpKind::Prune:
      return tr(STR_GIT_SYNC_STEP_PRUNE);
    case SyncOpKind::Skip:
      return tr(STR_GIT_SYNC_STEP_SKIP);
  }
  return tr(STR_GIT_SYNC_STEP_DOWNLOAD);
}

const char* phaseLabel(const GitSyncPhase phase, const SyncOpKind kind) {
  switch (phase) {
    case GitSyncPhase::Preparing:
      return tr(STR_GIT_SYNC_PREPARING);
    case GitSyncPhase::FetchingTree:
      return tr(STR_GIT_SYNC_FETCHING_TREE);
    case GitSyncPhase::Planning:
      return tr(STR_GIT_SYNC_PLANNING);
    case GitSyncPhase::Applying:
      return opKindLabel(kind);
  }
  return tr(STR_GIT_SYNC_PREPARING);
}

const char* errorTitle(const GitSyncError error) {
  switch (error) {
    case GitSyncError::Ok:
      return tr(STR_GIT_SYNC_COMPLETE);
    case GitSyncError::Cancelled:
      return tr(STR_GIT_SYNC_STOPPED);
    case GitSyncError::OutOfMemory:
    case GitSyncError::StorageError:
    case GitSyncError::ManifestCorrupt:
    case GitSyncError::TreeDownloadFailed:
    case GitSyncError::TreeInvalid:
    case GitSyncError::TreeTruncated:
    case GitSyncError::TooManyFiles:
    case GitSyncError::RedirectRefused:
    case GitSyncError::TooManyFetchFailures:
      return tr(STR_SYNC_FAILED_MSG);
  }
  return tr(STR_SYNC_FAILED_MSG);
}

// Empty for Ok, whose screen shows the counts instead.
void errorDetail(const GitSyncError error, char* out, const size_t outSize) {
  const char* text = "";
  switch (error) {
    case GitSyncError::Ok:
      break;
    case GitSyncError::Cancelled:
      text = tr(STR_GIT_SYNC_ERR_CANCELLED);
      break;
    case GitSyncError::OutOfMemory:
      text = tr(STR_GIT_SYNC_ERR_MEMORY);
      break;
    case GitSyncError::StorageError:
      text = tr(STR_GIT_SYNC_ERR_STORAGE);
      break;
    case GitSyncError::ManifestCorrupt:
      text = tr(STR_GIT_SYNC_ERR_MANIFEST);
      break;
    case GitSyncError::TreeDownloadFailed:
      text = tr(STR_GIT_SYNC_ERR_TREE_DOWNLOAD);
      break;
    case GitSyncError::TreeInvalid:
      text = tr(STR_GIT_SYNC_ERR_TREE_INVALID);
      break;
    case GitSyncError::TreeTruncated:
      text = tr(STR_GIT_SYNC_ERR_TREE_TRUNCATED);
      break;
    case GitSyncError::TooManyFiles:
      snprintf(out, outSize, tr(STR_GIT_SYNC_ERR_TOO_MANY_FILES_FORMAT), static_cast<unsigned>(MAX_SYNC_FILES));
      return;
    case GitSyncError::RedirectRefused:
      text = tr(STR_GIT_SYNC_ERR_REDIRECT);
      break;
    case GitSyncError::TooManyFetchFailures:
      text = tr(STR_GIT_SYNC_ERR_NETWORK);
      break;
  }
  snprintf(out, outSize, "%s", text);
}

// Copies the last segment of a repository path, or clears out when there is none.
void copyFileName(const char* repoPath, char* out, const size_t outSize) {
  if (repoPath == nullptr) {
    out[0] = '\0';
    return;
  }
  const char* slash = strrchr(repoPath, '/');
  const char* name = slash != nullptr ? slash + 1 : repoPath;
  const size_t len = strnlen(name, outSize - 1);
  memcpy(out, name, len);
  out[len] = '\0';
}

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
  {
    RenderLock lock(*this);
    pending = ProgressSnapshot{};
    shown = pending;
    state = State::Running;
  }
  requestUpdateAndWait();

  lowestFreeHeap = ESP.getFreeHeap();
  LOG_INF("GSYNC", "Heap before sync: free %u, largest block %u", static_cast<unsigned>(lowestFreeHeap),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  lastPublishMs = millis();

  const GitSyncReport result =
      git_sync_engine::run(config, GitSyncProgressSink{this, &GitSyncActivity::onSyncProgress});
  // No rerun from this activity, so the token is not needed any more.
  wipeConfig();

  LOG_INF("GSYNC", "Lowest free heap during sync: %u", static_cast<unsigned>(lowestFreeHeap));
  LOG_INF("GSYNC", "Loop task stack high-water mark: %u bytes",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

  {
    RenderLock lock(*this);
    report = result;
    state = State::Done;
  }
  if (goHomeAfterRun) {
    onGoHome();
    return;
  }
  requestUpdate();
}

bool GitSyncActivity::onSyncProgress(void* ctx, const GitSyncProgress& progress) {
  return static_cast<GitSyncActivity*>(ctx)->handleProgress(progress);
}

// Runs on the loop task inside the blocking engine call: allocates nothing and takes the render lock only to publish.
bool GitSyncActivity::handleProgress(const GitSyncProgress& progress) {
  resetTaskWatchdogIfSubscribed();

  // The activity loop is blocked, so input is pumped here. This update() consumes the one-shot home event, which is
  // honoured after the run.
  const bool wasCancelling = cancelRequested;
  mappedInput.update();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancelRequested = true;
  }
  if (mappedInput.wasHomeGesture()) {
    cancelRequested = true;
    goHomeAfterRun = true;
  }

  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < lowestFreeHeap) {
    lowestFreeHeap = freeHeap;
  }

  const bool phaseChanged = progress.phase != pending.phase;
  pending.phase = progress.phase;
  pending.kind = progress.kind;
  pending.cancelling = cancelRequested;
  pending.done = progress.done;
  pending.total = progress.total;
  pending.treeEntries = progress.treeEntries;
  if (!progress.tick) {
    copyFileName(progress.repoPath, pending.name, sizeof(pending.name));
  }

  const unsigned long now = millis();
  if (phaseChanged || (cancelRequested && !wasCancelling) || now - lastPublishMs >= GIT_SYNC_PROGRESS_MIN_REDRAW_MS) {
    lastPublishMs = now;
    publishProgress();
  }
  return !cancelRequested;
}

void GitSyncActivity::publishProgress() {
  {
    RenderLock lock(*this);
    shown = pending;
  }
  // Immediate: the deferred flag is read only at the end of a loop iteration, and this one is blocked.
  requestUpdate(true);
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
    case State::Running: {
      renderProgress(lines);
      const auto labels = mappedInput.mapLabels(shown.cancelling ? "" : tr(STR_CANCEL), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer();
      return;
    }
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
  const ProgressSnapshot& progress = shown;
  drawLine(lines, phaseLabel(progress.phase, progress.kind), EpdFontFamily::BOLD);

  if (progress.phase == GitSyncPhase::FetchingTree && progress.treeEntries > 0) {
    char line[GIT_SYNC_LINE_BYTES];
    snprintf(line, sizeof(line), tr(STR_GIT_SYNC_TREE_ENTRIES_FORMAT), static_cast<unsigned>(progress.treeEntries));
    drawLine(lines, line);
  }

  if (progress.phase == GitSyncPhase::Applying && progress.total > 0) {
    drawProgressBar(lines, progress.done, progress.total);
    char line[GIT_SYNC_PROGRESS_LINE_BYTES];
    snprintf(line, sizeof(line), tr(STR_GIT_SYNC_PROGRESS_FORMAT), static_cast<unsigned>(progress.done),
             static_cast<unsigned>(progress.total), progress.name);
    drawLine(lines, line);
  }

  if (progress.cancelling) {
    drawLine(lines, tr(STR_GIT_SYNC_CANCELLING));
  }
}

void GitSyncActivity::renderDone(LineStack& lines) const {
  const SyncRunResult& run = report.run;
  const bool anyCount = run.added != 0 || run.updated != 0 || run.moved != 0 || run.deleted != 0 || run.adopted != 0;

  drawLine(lines, errorTitle(report.error), EpdFontFamily::BOLD);
  if (report.error == GitSyncError::Ok) {
    if (!anyCount && run.fileErrors == 0) {
      drawLine(lines, tr(STR_GIT_SYNC_UP_TO_DATE));
    } else {
      drawCounts(lines);
    }
  } else {
    char detail[GIT_SYNC_LINE_BYTES];
    errorDetail(report.error, detail, sizeof(detail));
    drawWrapped(lines, detail, GIT_SYNC_DETAIL_MAX_LINES);
    if (anyCount) {
      drawCounts(lines);
    }
  }

  char line[GIT_SYNC_LINE_BYTES];
  if (run.fileErrors != 0) {
    snprintf(line, sizeof(line), tr(STR_GIT_SYNC_FILE_ERRORS_FORMAT), static_cast<unsigned>(run.fileErrors));
    drawLine(lines, line);
  }
  const uint32_t skipped = run.skippedCaseCollision + run.skippedUnsafeName;
  if (skipped != 0) {
    snprintf(line, sizeof(line), tr(STR_GIT_SYNC_SKIPPED_FORMAT), static_cast<unsigned>(skipped));
    drawLine(lines, line);
  }
}

void GitSyncActivity::drawCounts(LineStack& lines) const {
  const SyncRunResult& run = report.run;
  char line[GIT_SYNC_LINE_BYTES];
  snprintf(line, sizeof(line), tr(STR_GIT_SYNC_ADDED_FORMAT), static_cast<unsigned>(run.added));
  drawLine(lines, line);
  snprintf(line, sizeof(line), tr(STR_GIT_SYNC_UPDATED_FORMAT), static_cast<unsigned>(run.updated));
  drawLine(lines, line);
  snprintf(line, sizeof(line), tr(STR_GIT_SYNC_MOVED_FORMAT), static_cast<unsigned>(run.moved));
  drawLine(lines, line);
  snprintf(line, sizeof(line), tr(STR_GIT_SYNC_DELETED_FORMAT), static_cast<unsigned>(run.deleted));
  drawLine(lines, line);
  snprintf(line, sizeof(line), tr(STR_GIT_SYNC_ADOPTED_FORMAT), static_cast<unsigned>(run.adopted));
  drawLine(lines, line);
}

// The bar plus the percentage the theme draws under it.
bool GitSyncActivity::drawProgressBar(LineStack& lines, const uint32_t done, const uint32_t total) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int height = metrics.progressBarHeight + metrics.verticalSpacing + renderer.getLineHeight(UI_10_FONT_ID);
  if (lines.y + height > lines.area.y + lines.area.height) {
    return false;
  }
  GUI.drawProgressBar(renderer, Rect{lines.area.x, lines.y, lines.area.width, metrics.progressBarHeight}, done, total);
  lines.y += height + metrics.verticalSpacing;
  return true;
}

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
