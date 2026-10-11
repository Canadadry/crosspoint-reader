#pragma once

#include <GitSyncConfig.h>

#include <cstdint>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"

// Update screen: loads /.crosspoint/github-sync.json, brings Wi-Fi up as the OPDS browser does, runs one GitHub
// sync from loop() and reports it. Leaving with Wi-Fi on restarts silently to home.
class GitSyncActivity final : public Activity {
 public:
  enum class State : uint8_t { ConfigError, WifiSelection, WifiFailed, Starting, Running, Done };

  explicit GitSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GitSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Always true: the main loop's inactivity timer is stale after a long blocking run.
  bool preventAutoSleep() override { return true; }

 private:
  // Text lines stacked down the content area; a line that would cross its bottom is not drawn.
  struct LineStack {
    Rect area;
    int y;
  };

  State state = State::Starting;
  // Holds the token: a member (never a stack local), wiped once the run is over and in onExit.
  GitSyncConfig config{};

  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void runSync();
  void wipeConfig();
  void setState(State next);
  bool endScreenDismissed() const;

  void renderProgress(LineStack& lines) const;
  void renderDone(LineStack& lines) const;
  bool drawLine(LineStack& lines, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawWrapped(LineStack& lines, const char* text, int maxLines) const;
};
