#pragma once

#include <GitSyncConfig.h>

// Device side of /.crosspoint/github-sync.json: reads it through HalStorage and validates it with
// parseGitSyncConfig. Logs only constant text, never a value of the token.
namespace git_sync_config {

constexpr char CONFIG_PATH[] = "/.crosspoint/github-sync.json";

// Fills out on Ok; on any other verdict out is all zero. A missing file is NotFound and not logged.
GitSyncConfigResult load(GitSyncConfig& out);

// True only when the file is present and valid. Re-reads the file on every call.
bool exists();

}  // namespace git_sync_config
