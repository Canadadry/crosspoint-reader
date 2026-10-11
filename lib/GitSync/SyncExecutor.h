#pragma once

#include <BookCachePath.h>

#include <cstddef>
#include <cstdint>
#include <optional>

#include "GitBlobHasher.h"
#include "RecordFormat.h"
#include "RecordReader.h"
#include "RecordWriter.h"
#include "SyncPath.h"
#include "SyncPlanner.h"
#include "SyncStorage.h"

enum class SyncRunStatus : uint8_t {
  Ok,
  Cancelled,
  StorageError,
  ManifestError,
  TreeError,
  CapExceeded,
  OutOfMemory,
  NotPrepared,
};

struct SyncRunResult {
  SyncRunStatus status = SyncRunStatus::Ok;
  // The reader's status behind a ManifestError or a TreeError.
  RecordStatus recordStatus = RecordStatus::End;
  // The planner's counts behind a CapExceeded.
  uint32_t manifestEntries = 0;
  uint32_t treeEntries = 0;
  uint32_t added = 0;
  uint32_t updated = 0;
  uint32_t moved = 0;
  uint32_t deleted = 0;
  uint32_t adopted = 0;
  uint32_t skippedCaseCollision = 0;
  uint32_t skippedUnsafeName = 0;
  // Operations abandoned with the manifest unchanged; the next run retries them.
  uint32_t fileErrors = 0;
  // Reading state left in place because the destination already held its own.
  uint32_t stateConflicts = 0;
  // Directories removed because a sync emptied them.
  uint32_t pruned = 0;
};

// Applied and Skipped continue the run; the per-file errors leave the manifest unchanged and continue;
// Fatal stops the run with StorageError.
enum class SyncOpOutcome : uint8_t {
  Applied,
  Skipped,
  FetchFailed,
  VerifyFailed,
  RefusedPath,
  TargetUnwritable,
  Fatal,
};

// sha and size are the tree entry's; repoPath is repository-relative, for logs and progress.
struct SyncFetchRequest {
  const uint8_t* sha;
  uint64_t size;
  const char* repoPath;
};

// write returns false when the bytes cannot be stored or exceed the declared size; the fetch then stops.
struct SyncChunkSink {
  void* ctx;
  bool (*write)(void* ctx, const uint8_t* data, size_t len);
};

// done counts operations started, 1 to total; total is the planner's onTotal value.
struct SyncProgress {
  uint32_t done;
  uint32_t total;
  SyncOpKind kind;
  const char* repoPath;
};

// Every side effect outside the card. Paths given to the hooks are absolute. Any pointer may be null:
// no fetch fails every non-empty download, no hashFile downloads instead of adopting, no hook does nothing.
struct SyncExecutorCallbacks {
  void* ctx;
  // True only when the body ended normally. Never called for a size-0 entry.
  bool (*fetch)(void* ctx, const SyncFetchRequest& request, const SyncChunkSink& sink);
  // The local file's git blob SHA; false means unknown.
  bool (*hashFile)(void* ctx, const char* absPath, uint64_t expectedSize, uint8_t outSha[GitBlobHasher::SHA_SIZE]);
  // Idempotent; false is fatal.
  bool (*clearBookCache)(void* ctx, const char* absPath);
  // Idempotent; false is fatal.
  bool (*bookMoved)(void* ctx, const char* oldAbsPath, const char* newAbsPath);
  void (*onProgress)(void* ctx, const SyncProgress& progress);
};

// Applies the planner's operations to the card. One executor per sync: prepare(), then apply(), or
// begin(), operations through sink() or execute(), then end().
// Every operation ends with exactly one journal frame, written last: a run that stops anywhere before it
// leaves the manifest saying "not done", and every earlier step is idempotent, so the next run redoes it.
// No heap except the compaction scratch in prepare(), freed before it returns. Allocate the executor
// with makeUniqueNoThrow, never on the stack.
class SyncExecutor {
 public:
  SyncExecutor(SyncStorage& storage, const SyncExecutorCallbacks& callbacks);
  ~SyncExecutor();

  SyncExecutor(const SyncExecutor&) = delete;
  SyncExecutor& operator=(const SyncExecutor&) = delete;

  // Repairs what a crash left in the work directory, creates an empty manifest on the first sync,
  // and compacts the manifest's journal. Call before the planner exists.
  SyncRunResult prepare();

  // begin(), the planner's plan over the manifest and tree.tmp through sink(), then end(). Call after a
  // successful prepare(), once tree.tmp is written.
  SyncRunResult apply(SyncPlanner& planner);

  // Opens the manifest and the tree for operations; false with the reason in end()'s result.
  bool begin();
  // The sink apply() hands the planner: onTotal records the total, onOp checks the cancel flag,
  // reports progress, executes, and refuses on cancel or on a fatal outcome.
  SyncOpSink sink();
  // Executes one operation, without progress or cancel check.
  SyncOpOutcome execute(const SyncOp& op);
  // Closes the files and returns the run's status and counts.
  SyncRunResult end();

  // Honoured between operations; an operation in progress completes or fails as a whole.
  void requestCancel() { cancelRequested = true; }

 private:
  bool startOp(const SyncOp& op);
  bool loadPaths(const SyncOp& op);
  bool loadEntry(RecordReader& reader, uint32_t offset, char* absPath, bool keepEntry);
  SyncOpOutcome run(const SyncOp& op);
  SyncOpOutcome download(uint32_t replaces);
  SyncOpOutcome land(uint32_t replaces);
  SyncOpOutcome adoptOrDownload();
  bool matchesTree();
  SyncOpOutcome move(uint32_t replaces);
  bool makeParent(char* absPath);
  SyncOpOutcome migrateState();
  SyncOpOutcome moveState();
  bool formatCacheDirs(BookCacheKind kind);
  SyncOpOutcome remove(uint32_t manifestOffset);
  SyncOpOutcome prune(char* absPath);
  SyncOpOutcome skip(SyncSkipReason reason);
  SyncOpOutcome fileError(SyncOpOutcome outcome);
  SyncOpOutcome dropTemp(SyncOpOutcome outcome);
  SyncOpOutcome fatal(const char* what, const char* path);
  SyncOpOutcome fetchIntoTemp();
  SyncOpOutcome journalPut(uint32_t replaces);
  SyncOpOutcome journalFlush();
  bool clearCache(const char* absPath);
  void closeFiles();

  SyncRunStatus recoverManifest();
  bool isCleanManifest(const char* path);
  SyncRunStatus checkManifest(bool& needsCompaction);
  SyncRunStatus compactManifest();
  bool writeEmptyManifest();

  static bool localExists(void* ctx, const char* path, size_t pathLen);
  static bool sinkOp(void* ctx, const SyncOp& op);
  static void sinkTotal(void* ctx, uint32_t totalOps);
  static bool sinkWrite(void* ctx, const uint8_t* data, size_t len);

  SyncStorage& storage;
  SyncExecutorCallbacks callbacks;
  SyncRunResult result;
  SyncFile* manifestFile = nullptr;
  SyncFile* treeFile = nullptr;
  SyncFile* tempFile = nullptr;
  std::optional<RecordReader> manifestReader;
  std::optional<RecordReader> treeReader;
  std::optional<RecordWriter> journal;
  GitBlobHasher hasher;
  uint32_t endOffset = 0;
  uint32_t total = 0;
  uint32_t done = 0;
  uint64_t fetched = 0;
  bool prepared = false;
  bool failed = false;
  bool cancelled = false;
  bool tempWriteFailed = false;
  bool overflow = false;
  volatile bool cancelRequested = false;
  // The tree entry an operation names, and its absolute paths with their terminators.
  uint8_t entrySha[GitBlobHasher::SHA_SIZE] = {};
  uint64_t entrySize = 0;
  char treePath[1 + MAX_SYNC_PATH_BYTES + 1] = {};
  char manifestPath[1 + MAX_SYNC_PATH_BYTES + 1] = {};
  // The bookmark path is the longest reading-state path; a cache dir is shorter.
  static constexpr size_t STATE_PATH_CAPACITY =
      sizeof("/.crosspoint/bookmarks/") - 1 + MAX_SYNC_PATH_BYTES + sizeof(".json");
  // One reading-state path of the manifest path's book, and the same of the tree path's.
  char oldStatePath[STATE_PATH_CAPACITY] = {};
  char newStatePath[STATE_PATH_CAPACITY] = {};
};

static_assert(sizeof(SyncExecutor) <= 3072, "SyncExecutor exceeds its 3 KiB share of the 64 KB sync heap");
