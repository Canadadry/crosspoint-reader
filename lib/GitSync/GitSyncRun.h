#pragma once

#include <cstddef>
#include <cstdint>

#include "SyncExecutor.h"
#include "SyncStorage.h"

// Ok: the body ended normally. Failed: the sink refused the body. NetworkError: connection, TLS or HTTP status
// failure. RedirectRefused: a redirect to another origin was refused. Aborted: stopped because the run asked.
enum class GitFetchStatus : uint8_t { Ok, Failed, NetworkError, RedirectRefused, Aborted };

// The network. A transfer ends when the sink returns false.
struct GitSyncTransport {
  void* ctx;
  GitFetchStatus (*fetchTree)(void* ctx, const SyncChunkSink& sink);
  GitFetchStatus (*fetchBlob)(void* ctx, const SyncFetchRequest& request, const SyncChunkSink& sink);
};

// Reading state outside the card, given absolute paths; both idempotent, false is fatal. A null pointer does nothing.
struct GitSyncHooks {
  void* ctx;
  bool (*clearBookCache)(void* ctx, const char* absPath);
  bool (*bookMoved)(void* ctx, const char* oldAbsPath, const char* newAbsPath);
};

enum class GitSyncPhase : uint8_t { Preparing, FetchingTree, Planning, Applying };

// tick: a heartbeat during a transfer or a hash, nothing changed; false on a phase change or a new operation.
// done, total, kind and repoPath describe the current operation during Applying (repoPath null before the first);
// treeEntries counts the tree entries written so far.
struct GitSyncProgress {
  GitSyncPhase phase;
  bool tick;
  uint32_t done;
  uint32_t total;
  SyncOpKind kind;
  const char* repoPath;
  uint32_t treeEntries;
};

// Returning false requests a cancel, latched for the rest of the run. A null sink or function never cancels.
struct GitSyncProgressSink {
  void* ctx;
  bool (*onProgress)(void* ctx, const GitSyncProgress& progress);
};

enum class GitSyncError : uint8_t {
  Ok,
  Cancelled,
  OutOfMemory,
  StorageError,
  ManifestCorrupt,
  TreeDownloadFailed,
  TreeInvalid,
  TreeTruncated,
  TooManyFiles,
  RedirectRefused,
  TooManyFetchFailures,
};

// A constant English string per value, for logs only.
const char* describeGitSyncError(GitSyncError error);

// The tree snapshot writer's counters, zero when the tree phase was not reached.
struct GitSyncTreeCounts {
  uint32_t written = 0;
  uint32_t hidden = 0;
  uint32_t malformed = 0;
  uint32_t tooLong = 0;
  uint32_t skippedLongPath = 0;
  uint32_t skippedEscapedPath = 0;
};

// Ok with run.fileErrors > 0 is a completed sync whose failed files the next run retries.
struct GitSyncReport {
  GitSyncError error = GitSyncError::Ok;
  SyncRunResult run;
  GitSyncTreeCounts tree;
};

// Consecutive blob NetworkErrors that stop the run with TooManyFetchFailures; an Ok fetch resets the count.
constexpr uint8_t MAX_CONSECUTIVE_FETCH_FAILURES = 3;
// Read size of the adoption hash, allocated once per run.
constexpr size_t GIT_SYNC_HASH_BUFFER_BYTES = 4096;
// Upper bound of the run's own state, allocated once per run, for the glue's budget line.
constexpr size_t GIT_SYNC_RUN_STATE_MAX_BYTES = 512;

// Prepares the work directory, writes tree.tmp from transport.fetchTree, then plans and applies it, fetching blobs
// through transport.fetchBlob and hashing local files for adoption. tree.tmp is gone when it returns. Every report
// other than Ok leaves one LOG_ERR naming its error.
GitSyncReport runGitSync(SyncStorage& storage, const GitSyncTransport& transport, const GitSyncHooks& hooks,
                         const GitSyncProgressSink& progress);
