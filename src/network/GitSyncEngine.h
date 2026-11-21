#pragma once

#include <GitSyncConfig.h>
#include <GitSyncRun.h>

// Device side of a GitHub sync: runGitSync over the SD card, HttpDownloader and the reading-state stores.
namespace git_sync_engine {

// The last failed blob request of each kind: from the API (from byte 0) and ranged from the raw host.
// error is the HttpDownloader::DownloadError, status the HTTP status (-1 none read, 0 no request).
struct BlobFetchFailure {
  uint8_t error = 0;
  int16_t status = 0;
};
// BlobFetchFailure::error when no ranged request was sent because the raw URL did not fit.
constexpr uint8_t RAW_URL_TOO_LONG = 99;

struct Diagnostics {
  BlobFetchFailure api;
  BlobFetchFailure ranged;
};

// Runs one whole sync. The caller owns config (from git_sync_config::load) and is already on Wi-Fi. The token is
// sent only as the Basic auth password to api.github.com, never logged, and its copy is wiped before returning.
// Logs the report: LOG_INF with the counts on Ok, LOG_ERR naming the error otherwise.
// diagnostics, when set, receives the run's last failed blob requests.
GitSyncReport run(const GitSyncConfig& config, const GitSyncProgressSink& progress, Diagnostics* diagnostics = nullptr);

}  // namespace git_sync_engine
