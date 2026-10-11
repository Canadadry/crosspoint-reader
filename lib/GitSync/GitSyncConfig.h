#pragma once

#include <cstddef>
#include <cstdint>

// Contents of /.crosspoint/github-sync.json. owner, repo and branch need no escaping in a GitHub
// API URL. 653 bytes: hold it as a member or through makeUniqueNoThrow, never as a local.
struct GitSyncConfig {
  static constexpr size_t OWNER_MAX = 39;    // GitHub login / organisation name limit
  static constexpr size_t REPO_MAX = 100;    // GitHub repository name limit
  static constexpr size_t BRANCH_MAX = 255;  // bytes; far above real branch names
  static constexpr size_t TOKEN_MAX = 255;   // GitHub's documented upper bound for token length
  // Larger input is refused before parsing; every valid config fits, pretty-printed with CRLF.
  static constexpr size_t MAX_CONFIG_FILE_BYTES = 1024;

  char owner[OWNER_MAX + 1];
  char repo[REPO_MAX + 1];
  char branch[BRANCH_MAX + 1];
  char token[TOKEN_MAX + 1];
};

static_assert(sizeof(GitSyncConfig) <= 656, "GitSyncConfig grew");

// NotFound and ReadError come from the device loader only.
enum class GitSyncConfigStatus : uint8_t {
  Ok,
  NotFound,
  ReadError,
  TooLarge,
  Malformed,
  MissingField,
  InvalidField,
  OutOfMemory
};
enum class GitSyncConfigField : uint8_t { None, Owner, Repo, Branch, Token };

// field is set for MissingField and InvalidField, None otherwise.
struct GitSyncConfigResult {
  GitSyncConfigStatus status;
  GitSyncConfigField field;
};

// Parses exactly len bytes (no terminator needed). On any verdict but Ok, out is all zero.
// Never logs.
GitSyncConfigResult parseGitSyncConfig(const char* json, size_t len, GitSyncConfig& out);

// A constant string per (status, field), never built from a value: safe to log.
const char* describeGitSyncConfigResult(GitSyncConfigResult result);
