#pragma once

#include <GitSyncConfig.h>
#include <GitSyncRun.h>

// Device side of a GitHub sync: runGitSync over the SD card, HttpDownloader and the reading-state stores.
namespace git_sync_engine {

// Runs one whole sync. The caller owns config (from git_sync_config::load) and is already on Wi-Fi. The token is
// sent only as the Basic auth password to api.github.com, never logged, and its copy is wiped before returning.
// Logs the report: LOG_INF with the counts on Ok, LOG_ERR naming the error otherwise.
GitSyncReport run(const GitSyncConfig& config, const GitSyncProgressSink& progress);

}  // namespace git_sync_engine
