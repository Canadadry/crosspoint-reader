#pragma once

#include <GitSyncConfig.h>
#include <GitSyncRun.h>
#include <SyncPath.h>

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

  // What the Running screen shows. The name is the last segment of the repository path, copied because the
  // engine's pointer is valid only during the callback.
  struct ProgressSnapshot {
    GitSyncPhase phase = GitSyncPhase::Preparing;
    SyncOpKind kind = SyncOpKind::Download;
    bool cancelling = false;
    uint32_t done = 0;
    uint32_t total = 0;
    uint32_t treeEntries = 0;
    char name[MAX_SYNC_PATH_BYTES + 1] = {};
  };
  static_assert(sizeof(ProgressSnapshot) <= 288, "progress snapshot grew");

  static constexpr unsigned long GIT_SYNC_PROGRESS_MIN_REDRAW_MS = 2000;

  State state = State::Starting;
  // Holds the token: a member (never a stack local), wiped once the run is over and in onExit.
  GitSyncConfig config{};
  // Loop task only, updated on every progress callback.
  ProgressSnapshot pending{};
  // Read by the render task; written under the render lock.
  ProgressSnapshot shown{};
  GitSyncReport report{};
  bool cancelRequested = false;
  bool goHomeAfterRun = false;
  uint32_t lowestFreeHeap = 0;
  unsigned long lastPublishMs = 0;

  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void runSync();
  static bool onSyncProgress(void* ctx, const GitSyncProgress& progress);
  bool handleProgress(const GitSyncProgress& progress);
  void publishProgress();
  void wipeConfig();
  void setState(State next);
  bool endScreenDismissed() const;

  void renderProgress(LineStack& lines) const;
  void renderDone(LineStack& lines) const;
  void drawCounts(LineStack& lines) const;
  bool drawProgressBar(LineStack& lines, uint32_t done, uint32_t total) const;
  bool drawLine(LineStack& lines, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawWrapped(LineStack& lines, const char* text, int maxLines) const;
};
