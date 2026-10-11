#include "GitSyncRun.h"

#include <Logging.h>
#include <Memory.h>

#include "GitBlobSourceHash.h"
#include "SyncPlanner.h"
#include "TreeSnapshotWriter.h"

namespace {
constexpr const char* LOG_TAG = "GITSYNC";

class SyncRun {
 public:
  SyncRun(SyncStorage& storage, const GitSyncTransport& transport, const GitSyncHooks& hooks,
          const GitSyncProgressSink& progress)
      : storage(storage), transport(transport), hooks(hooks), progress(progress) {}

  GitSyncReport run() {
    GitSyncReport report;
    report.error = execute(report);
    if (report.error != GitSyncError::Ok) {
      LOG_ERR(LOG_TAG, "Sync ended: %s (run status %u, record status %u)", describeGitSyncError(report.error),
              static_cast<unsigned>(report.run.status), static_cast<unsigned>(report.run.recordStatus));
    }
    return report;
  }

 private:
  void enterPhase(GitSyncPhase phase) {
    current.phase = phase;
    notify(false);
  }

  // A false answer latches the cancel: the tree writer stops the transfer, the executor stops between operations.
  void notify(bool tick) {
    current.tick = tick;
    if (!progress.onProgress || progress.onProgress(progress.ctx, current) || cancelLatched) return;
    cancelLatched = true;
    if (treeWriter) treeWriter->cancel();
    executor->requestCancel();
  }

  // The phases in order. The planner exists only once the tree writer is gone. tree.tmp is removed before the tree
  // phase and after it; on a failing card the next run removes it.
  GitSyncError execute(GitSyncReport& report) {
    executor = makeUniqueNoThrow<SyncExecutor>(
        storage, SyncExecutorCallbacks{this, &fetch, &hashFile, hooks.clearBookCache ? &clearBookCache : nullptr,
                                       hooks.bookMoved ? &bookMoved : nullptr, &onOperation});
    if (!executor) return outOfMemory("executor", sizeof(SyncExecutor));
    hashBuffer = makeUniqueNoThrow<uint8_t[]>(GIT_SYNC_HASH_BUFFER_BYTES);
    if (!hashBuffer) return outOfMemory("hash buffer", GIT_SYNC_HASH_BUFFER_BYTES);

    enterPhase(GitSyncPhase::Preparing);
    report.run = executor->prepare();
    if (report.run.status != SyncRunStatus::Ok) return runError(report.run.status);
    if (storage.exists(SYNC_TREE_PATH) && !storage.remove(SYNC_TREE_PATH)) {
      LOG_ERR(LOG_TAG, "Cannot remove the leftover %s", SYNC_TREE_PATH);
      return GitSyncError::StorageError;
    }
    if (cancelLatched) return GitSyncError::Cancelled;

    enterPhase(GitSyncPhase::FetchingTree);
    GitSyncError error = fetchTree(report.tree);
    if (error == GitSyncError::Ok && cancelLatched) error = GitSyncError::Cancelled;
    if (error == GitSyncError::Ok) error = planAndApply(report.run);
    if (storage.exists(SYNC_TREE_PATH) && !storage.remove(SYNC_TREE_PATH)) {
      LOG_ERR(LOG_TAG, "Cannot remove %s", SYNC_TREE_PATH);
      if (error == GitSyncError::Ok) error = GitSyncError::StorageError;
    }
    return error;
  }

  GitSyncError planAndApply(SyncRunResult& result) {
    enterPhase(GitSyncPhase::Planning);
    auto planner = makeUniqueNoThrow<SyncPlanner>();
    if (!planner) return outOfMemory("planner", sizeof(SyncPlanner));
    result = executor->apply(*planner);
    return runError(result.status);
  }

  static GitSyncError outOfMemory(const char* what, size_t bytes) {
    LOG_ERR(LOG_TAG, "OOM: %s needs %u bytes", what, static_cast<unsigned>(bytes));
    return GitSyncError::OutOfMemory;
  }

  GitSyncError fetchTree(GitSyncTreeCounts& counts) {
    if (!transport.fetchTree) {
      LOG_ERR(LOG_TAG, "No tree transport");
      return GitSyncError::TreeDownloadFailed;
    }
    SyncFile* file = storage.open(SYNC_TREE_PATH, SyncOpenMode::Truncate);
    if (!file) {
      LOG_ERR(LOG_TAG, "Cannot open %s", SYNC_TREE_PATH);
      return GitSyncError::StorageError;
    }
    auto writer = makeUniqueNoThrow<TreeSnapshotWriter>(*file);
    if (!writer) {
      storage.close(file);
      return outOfMemory("tree writer", sizeof(TreeSnapshotWriter));
    }
    if (!writer->begin()) {
      storage.close(file);
      LOG_ERR(LOG_TAG, "Cannot write the header of %s", SYNC_TREE_PATH);
      return GitSyncError::StorageError;
    }
    treeWriter = writer.get();
    const GitFetchStatus fetched = transport.fetchTree(transport.ctx, SyncChunkSink{this, &treeChunk});
    const bool refused = writer->stopped();
    const TreeSnapshotStatus status = writer->finish();
    treeWriter = nullptr;
    counts.written = writer->entriesWritten();
    counts.hidden = writer->hiddenCount();
    counts.malformed = writer->malformedCount();
    counts.tooLong = writer->tooLongCount();
    counts.skippedLongPath = writer->skippedLongPathCount();
    counts.skippedEscapedPath = writer->skippedEscapedPathCount();
    writer.reset();
    const bool flushed = file->flush();
    storage.close(file);
    if (fetched != GitFetchStatus::Ok) {
      LOG_ERR(LOG_TAG, "Tree fetch ended with status %u%s", static_cast<unsigned>(fetched),
              refused ? " after the body was refused" : "");
    }
    const GitSyncError error = treeVerdict(status, fetched, refused);
    if (error == GitSyncError::Ok && !flushed) {
      LOG_ERR(LOG_TAG, "Cannot flush %s", SYNC_TREE_PATH);
      return GitSyncError::StorageError;
    }
    return error;
  }

  // The writer's own stops make the transport report a refused body, so they come first; a transfer that failed
  // on its own explains an incomplete body, so it comes before the parse verdicts.
  static GitSyncError treeVerdict(TreeSnapshotStatus status, GitFetchStatus fetched, bool refused) {
    switch (status) {
      case TreeSnapshotStatus::WriteFailed:
        return GitSyncError::StorageError;
      case TreeSnapshotStatus::TooManyFiles:
        return GitSyncError::TooManyFiles;
      case TreeSnapshotStatus::Cancelled:
        return GitSyncError::Cancelled;
      default:
        break;
    }
    if (fetched == GitFetchStatus::RedirectRefused) return GitSyncError::RedirectRefused;
    if (fetched != GitFetchStatus::Ok && !refused) return GitSyncError::TreeDownloadFailed;
    if (status == TreeSnapshotStatus::Invalid) return GitSyncError::TreeInvalid;
    if (status == TreeSnapshotStatus::Truncated) return GitSyncError::TreeTruncated;
    return GitSyncError::Ok;
  }

  static bool treeChunk(void* ctx, const uint8_t* data, size_t len) {
    auto* self = static_cast<SyncRun*>(ctx);
    self->treeWriter->feed(reinterpret_cast<const char*>(data), len);
    self->current.treeEntries = self->treeWriter->entriesWritten();
    self->notify(true);
    return !self->treeWriter->stopped();
  }

  static void onOperation(void* ctx, const SyncProgress& op) {
    auto* self = static_cast<SyncRun*>(ctx);
    self->current.phase = GitSyncPhase::Applying;
    self->current.done = op.done;
    self->current.total = op.total;
    self->current.kind = op.kind;
    self->current.repoPath = op.repoPath;
    self->notify(false);
  }

  GitSyncError runError(SyncRunStatus status) const {
    switch (status) {
      case SyncRunStatus::Ok:
      case SyncRunStatus::Cancelled:
        if (stopReason != GitSyncError::Ok) return stopReason;
        return status == SyncRunStatus::Ok ? GitSyncError::Ok : GitSyncError::Cancelled;
      case SyncRunStatus::ManifestError:
        return GitSyncError::ManifestCorrupt;
      case SyncRunStatus::CapExceeded:
        return GitSyncError::TooManyFiles;
      case SyncRunStatus::OutOfMemory:
        return GitSyncError::OutOfMemory;
      case SyncRunStatus::StorageError:
      case SyncRunStatus::TreeError:
      case SyncRunStatus::NotPrepared:
        return GitSyncError::StorageError;
    }
    return GitSyncError::StorageError;
  }

  static bool clearBookCache(void* ctx, const char* absPath) {
    const GitSyncHooks& hooks = static_cast<SyncRun*>(ctx)->hooks;
    return hooks.clearBookCache(hooks.ctx, absPath);
  }

  static bool bookMoved(void* ctx, const char* oldAbsPath, const char* newAbsPath) {
    const GitSyncHooks& hooks = static_cast<SyncRun*>(ctx)->hooks;
    return hooks.bookMoved(hooks.ctx, oldAbsPath, newAbsPath);
  }

  static void tick(void* ctx) { static_cast<SyncRun*>(ctx)->notify(true); }

  // A cancel latched during the transfer does not cut it: the file completes or fails as a whole.
  static bool fetch(void* ctx, const SyncFetchRequest& request, const SyncChunkSink& sink) {
    auto* self = static_cast<SyncRun*>(ctx);
    if (!self->transport.fetchBlob) {
      LOG_ERR(LOG_TAG, "No blob transport");
      return false;
    }
    self->blobSink = sink;
    const GitFetchStatus fetched =
        self->transport.fetchBlob(self->transport.ctx, request, SyncChunkSink{self, &blobChunk});
    switch (fetched) {
      case GitFetchStatus::Ok:
        self->networkErrors = 0;
        return true;
      case GitFetchStatus::Failed:
      case GitFetchStatus::Aborted:
        return false;
      case GitFetchStatus::NetworkError:
        if (++self->networkErrors >= MAX_CONSECUTIVE_FETCH_FAILURES) {
          LOG_ERR(LOG_TAG, "%u consecutive network failures: stopping the sync", self->networkErrors);
          self->stop(GitSyncError::TooManyFetchFailures);
        }
        return false;
      case GitFetchStatus::RedirectRefused:
        LOG_ERR(LOG_TAG, "Blob redirect refused: stopping the sync");
        self->stop(GitSyncError::RedirectRefused);
        return false;
    }
    return false;
  }

  // Every later file would fail the same way. RedirectRefused outranks TooManyFetchFailures.
  void stop(GitSyncError reason) {
    if (stopReason != GitSyncError::RedirectRefused) stopReason = reason;
    executor->requestCancel();
  }

  static bool blobChunk(void* ctx, const uint8_t* data, size_t len) {
    auto* self = static_cast<SyncRun*>(ctx);
    const bool written = self->blobSink.write(self->blobSink.ctx, data, len);
    self->notify(true);
    return written;
  }

  // Reads the local file through the storage's third slot: the executor holds the manifest and the tree.
  static bool hashFile(void* ctx, const char* absPath, uint64_t expectedSize, uint8_t outSha[GitBlobHasher::SHA_SIZE]) {
    auto* self = static_cast<SyncRun*>(ctx);
    SyncFile* file = self->storage.open(absPath, SyncOpenMode::Read);
    if (!file) return false;
    const bool hashed = hashGitBlobSource(*file, expectedSize, self->hashBuffer.get(), GIT_SYNC_HASH_BUFFER_BYTES,
                                          self->hasher, outSha, GitBlobHashTick{self, &tick});
    self->storage.close(file);
    return hashed;
  }

  SyncStorage& storage;
  const GitSyncTransport transport;
  const GitSyncHooks hooks;
  const GitSyncProgressSink progress;
  GitSyncProgress current{GitSyncPhase::Preparing, false, 0, 0, SyncOpKind::Skip, nullptr, 0};
  std::unique_ptr<uint8_t[]> hashBuffer;
  GitBlobHasher hasher;
  std::unique_ptr<SyncExecutor> executor;
  TreeSnapshotWriter* treeWriter = nullptr;
  SyncChunkSink blobSink{nullptr, nullptr};
  bool cancelLatched = false;
  // A network condition that stops the run; it outranks the user's cancel.
  GitSyncError stopReason = GitSyncError::Ok;
  unsigned networkErrors = 0;
};

static_assert(sizeof(SyncRun) <= GIT_SYNC_RUN_STATE_MAX_BYTES, "the run state outgrew GIT_SYNC_RUN_STATE_MAX_BYTES");
}  // namespace

const char* describeGitSyncError(GitSyncError error) {
  switch (error) {
    case GitSyncError::Ok:
      return "ok";
    case GitSyncError::Cancelled:
      return "cancelled";
    case GitSyncError::OutOfMemory:
      return "out of memory";
    case GitSyncError::StorageError:
      return "SD card error";
    case GitSyncError::ManifestCorrupt:
      return "manifest corrupt";
    case GitSyncError::TreeDownloadFailed:
      return "tree download failed";
    case GitSyncError::TreeInvalid:
      return "tree response invalid";
    case GitSyncError::TreeTruncated:
      return "tree truncated by GitHub";
    case GitSyncError::TooManyFiles:
      return "too many files";
    case GitSyncError::RedirectRefused:
      return "redirect to another host refused";
    case GitSyncError::TooManyFetchFailures:
      return "too many consecutive network failures";
  }
  return "unknown";
}

GitSyncReport runGitSync(SyncStorage& storage, const GitSyncTransport& transport, const GitSyncHooks& hooks,
                         const GitSyncProgressSink& progress) {
  auto run = makeUniqueNoThrow<SyncRun>(storage, transport, hooks, progress);
  if (!run) {
    GitSyncReport report;
    report.error = GitSyncError::OutOfMemory;
    LOG_ERR(LOG_TAG, "Sync ended: %s (run state, %u bytes)", describeGitSyncError(report.error),
            static_cast<unsigned>(sizeof(SyncRun)));
    return report;
  }
  return run->run();
}
