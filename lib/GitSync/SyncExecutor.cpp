#include "SyncExecutor.h"

#include <BookCachePath.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "BookmarkPath.h"
#include "ManifestCompactor.h"
#include "SyncPathFold.h"

namespace {

constexpr const char* LOG_TAG = "GITEXEC";

// The cache root every reader passes to bookCacheDir.
constexpr char BOOK_CACHE_ROOT[] = "/.crosspoint";
constexpr BookCacheKind BOOK_CACHE_KINDS[] = {BookCacheKind::Epub, BookCacheKind::Txt, BookCacheKind::Xtc};

// An empty buffer names no path; any other must be syncable once its leading '/' is dropped.
bool isSyncable(const char* absPath) { return absPath[0] == '\0' || isSyncablePath(absPath + 1, strlen(absPath + 1)); }

// Closes a storage file on every path out of a scope.
class OpenFile {
 public:
  OpenFile(SyncStorage& storage, const char* path, SyncOpenMode mode)
      : storage(storage), file(storage.open(path, mode)) {}
  ~OpenFile() { release(); }
  OpenFile(const OpenFile&) = delete;
  OpenFile& operator=(const OpenFile&) = delete;

  SyncFile* get() const { return file; }
  void release() {
    if (file) storage.close(file);
    file = nullptr;
  }

 private:
  SyncStorage& storage;
  SyncFile* file;
};

}  // namespace

SyncExecutor::SyncExecutor(SyncStorage& storage, const SyncExecutorCallbacks& callbacks)
    : storage(storage), callbacks(callbacks) {}

SyncExecutor::~SyncExecutor() { closeFiles(); }

SyncRunResult SyncExecutor::prepare() {
  result = SyncRunResult{};
  if (manifestFile) {
    LOG_ERR(LOG_TAG, "prepare() between begin() and end()");
    result.status = SyncRunStatus::StorageError;
    return result;
  }
  prepared = false;
  if (!storage.exists(SYNC_WORK_DIR) && !storage.mkdirs(SYNC_WORK_DIR)) {
    LOG_ERR(LOG_TAG, "Cannot create %s", SYNC_WORK_DIR);
    result.status = SyncRunStatus::StorageError;
    return result;
  }
  if (storage.exists(SYNC_DOWNLOAD_TMP_PATH) && !storage.remove(SYNC_DOWNLOAD_TMP_PATH)) {
    LOG_ERR(LOG_TAG, "Cannot remove %s", SYNC_DOWNLOAD_TMP_PATH);
    result.status = SyncRunStatus::StorageError;
    return result;
  }
  result.status = recoverManifest();
  bool needsCompaction = false;
  if (result.status == SyncRunStatus::Ok) result.status = checkManifest(needsCompaction);
  if (result.status == SyncRunStatus::Ok && needsCompaction) {
    result.status = compactManifest();
    // A rewrite beside the manifest is garbage; without the manifest it is the only copy, promoted next time.
    if (result.status != SyncRunStatus::Ok && storage.exists(SYNC_MANIFEST_PATH) &&
        storage.exists(SYNC_MANIFEST_NEW_PATH) && !storage.remove(SYNC_MANIFEST_NEW_PATH)) {
      LOG_ERR(LOG_TAG, "Cannot remove %s", SYNC_MANIFEST_NEW_PATH);
      result.status = SyncRunStatus::StorageError;
    }
  }
  prepared = result.status == SyncRunStatus::Ok;
  return result;
}

SyncRunStatus SyncExecutor::recoverManifest() {
  const bool rewriteExists = storage.exists(SYNC_MANIFEST_NEW_PATH);
  if (storage.exists(SYNC_MANIFEST_PATH)) {
    if (rewriteExists && !storage.remove(SYNC_MANIFEST_NEW_PATH)) {
      LOG_ERR(LOG_TAG, "Cannot remove %s", SYNC_MANIFEST_NEW_PATH);
      return SyncRunStatus::StorageError;
    }
    return SyncRunStatus::Ok;
  }
  // A crash between the manifest's remove and the rename of its rewrite.
  if (rewriteExists && isCleanManifest(SYNC_MANIFEST_NEW_PATH)) {
    if (storage.rename(SYNC_MANIFEST_NEW_PATH, SYNC_MANIFEST_PATH)) return SyncRunStatus::Ok;
    LOG_ERR(LOG_TAG, "Cannot promote %s", SYNC_MANIFEST_NEW_PATH);
    return SyncRunStatus::StorageError;
  }
  if (rewriteExists && !storage.remove(SYNC_MANIFEST_NEW_PATH)) {
    LOG_ERR(LOG_TAG, "Cannot remove %s", SYNC_MANIFEST_NEW_PATH);
    return SyncRunStatus::StorageError;
  }
  if (!writeEmptyManifest() || !storage.rename(SYNC_MANIFEST_NEW_PATH, SYNC_MANIFEST_PATH)) {
    LOG_ERR(LOG_TAG, "Cannot create an empty manifest");
    return SyncRunStatus::StorageError;
  }
  return SyncRunStatus::Ok;
}

// Reads to End as a sealed manifest with no journal and no torn tail.
bool SyncExecutor::isCleanManifest(const char* path) {
  OpenFile file(storage, path, SyncOpenMode::Read);
  if (!file.get()) return false;
  manifestReader.emplace(*file.get(), RecordFileKind::Manifest);
  SyncRecord record;
  RecordStatus status;
  bool clean = true;
  while ((status = manifestReader->next(record)) == RecordStatus::Record) {
    if (record.inJournal) clean = false;
  }
  clean = clean && status == RecordStatus::End && !manifestReader->tornTail();
  manifestReader.reset();
  return clean;
}

// Reads the manifest to End and records where the journal resumes.
SyncRunStatus SyncExecutor::checkManifest(bool& needsCompaction) {
  OpenFile file(storage, SYNC_MANIFEST_PATH, SyncOpenMode::Read);
  if (!file.get()) {
    LOG_ERR(LOG_TAG, "Cannot open %s", SYNC_MANIFEST_PATH);
    return SyncRunStatus::StorageError;
  }
  manifestReader.emplace(*file.get(), RecordFileKind::Manifest);
  SyncRecord record;
  RecordStatus status;
  needsCompaction = false;
  while ((status = manifestReader->next(record)) == RecordStatus::Record) {
    if (record.inJournal) needsCompaction = true;
  }
  if (manifestReader->tornTail()) needsCompaction = true;
  endOffset = manifestReader->endOffset();
  manifestReader.reset();
  if (status == RecordStatus::End) return SyncRunStatus::Ok;
  LOG_ERR(LOG_TAG, "Manifest unreadable: status %u", static_cast<unsigned>(status));
  if (status == RecordStatus::IoError) return SyncRunStatus::StorageError;
  result.recordStatus = status;
  return SyncRunStatus::ManifestError;
}

// Within one run every journal target names a distinct base entry, and the planner refuses a manifest above
// MAX_SYNC_FILES, so MAX_SYNC_FILES targets always fit the scratch.
SyncRunStatus SyncExecutor::compactManifest() {
  uint32_t compactedSize = 0;
  {
    OpenFile source(storage, SYNC_MANIFEST_PATH, SyncOpenMode::Read);
    OpenFile target(storage, SYNC_MANIFEST_NEW_PATH, SyncOpenMode::Truncate);
    if (!source.get() || !target.get()) {
      LOG_ERR(LOG_TAG, "Cannot open the manifest for compaction");
      return SyncRunStatus::StorageError;
    }
    auto compactor = makeUniqueNoThrow<ManifestCompactor>(*source.get(), *target.get());
    auto scratch = makeUniqueNoThrow<uint32_t[]>(MAX_SYNC_FILES);
    if (!compactor || !scratch) {
      LOG_ERR(LOG_TAG, "OOM: compaction needs %u bytes",
              static_cast<unsigned>(sizeof(ManifestCompactor) + MAX_SYNC_FILES * sizeof(uint32_t)));
      return SyncRunStatus::OutOfMemory;
    }
    const CompactResult compacted = compactor->compact(scratch.get(), MAX_SYNC_FILES);
    switch (compacted.status) {
      case CompactStatus::Ok:
        break;
      case CompactStatus::IoError:
      case CompactStatus::WriteFailed:
        LOG_ERR(LOG_TAG, "Compaction failed on the card: status %u", static_cast<unsigned>(compacted.status));
        return SyncRunStatus::StorageError;
      case CompactStatus::Truncated:
      case CompactStatus::Corrupt:
      case CompactStatus::WrongVersion:
      case CompactStatus::ScratchFull:
        LOG_ERR(LOG_TAG, "Manifest cannot be compacted: status %u", static_cast<unsigned>(compacted.status));
        result.recordStatus = compacted.status == CompactStatus::Truncated      ? RecordStatus::Truncated
                              : compacted.status == CompactStatus::WrongVersion ? RecordStatus::WrongVersion
                                                                                : RecordStatus::Corrupt;
        return SyncRunStatus::ManifestError;
    }
    if (!target.get()->flush()) {
      LOG_ERR(LOG_TAG, "Cannot flush %s", SYNC_MANIFEST_NEW_PATH);
      return SyncRunStatus::StorageError;
    }
    compactedSize = static_cast<uint32_t>(target.get()->size());
  }
  if (!storage.remove(SYNC_MANIFEST_PATH) || !storage.rename(SYNC_MANIFEST_NEW_PATH, SYNC_MANIFEST_PATH)) {
    LOG_ERR(LOG_TAG, "Cannot replace the manifest with its compaction");
    return SyncRunStatus::StorageError;
  }
  endOffset = compactedSize;
  return SyncRunStatus::Ok;
}

bool SyncExecutor::writeEmptyManifest() {
  OpenFile file(storage, SYNC_MANIFEST_NEW_PATH, SyncOpenMode::Truncate);
  if (!file.get()) return false;
  journal.emplace(*file.get());
  const bool ok = journal->begin(RecordFileKind::Manifest) && journal->seal() && file.get()->flush();
  journal.reset();
  return ok;
}

bool SyncExecutor::begin() {
  if (!prepared) {
    LOG_ERR(LOG_TAG, "begin() before a successful prepare()");
    result.status = SyncRunStatus::NotPrepared;
    return false;
  }
  if (manifestFile) {
    LOG_ERR(LOG_TAG, "begin() twice");
    return false;
  }
  result = SyncRunResult{};
  failed = false;
  cancelled = false;
  total = 0;
  done = 0;
  manifestFile = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Append);
  treeFile = storage.open(SYNC_TREE_PATH, SyncOpenMode::Read);
  if (!manifestFile || !treeFile || !manifestFile->seek(0) || !treeFile->seek(0)) {
    LOG_ERR(LOG_TAG, "Cannot open the manifest and the tree");
    closeFiles();
    result.status = SyncRunStatus::StorageError;
    return false;
  }
  manifestReader.emplace(*manifestFile, RecordFileKind::Manifest);
  treeReader.emplace(*treeFile, RecordFileKind::Tree);
  journal.emplace(*manifestFile);
  if (!journal->appendTo(endOffset)) {
    LOG_ERR(LOG_TAG, "Journal cannot resume at %u", static_cast<unsigned>(endOffset));
    closeFiles();
    result.status = SyncRunStatus::StorageError;
    return false;
  }
  return true;
}

SyncRunResult SyncExecutor::end() {
  if (manifestFile) {
    // A failed run may have torn the journal: only prepare() can tell where it resumes.
    if (failed) {
      prepared = false;
    } else {
      endOffset = static_cast<uint32_t>(manifestFile->size());
    }
  }
  closeFiles();
  if (result.status == SyncRunStatus::Ok) {
    if (failed) {
      result.status = SyncRunStatus::StorageError;
    } else if (cancelled) {
      result.status = SyncRunStatus::Cancelled;
    }
  }
  return result;
}

void SyncExecutor::closeFiles() {
  manifestReader.reset();
  treeReader.reset();
  journal.reset();
  for (SyncFile** file : {&tempFile, &treeFile, &manifestFile}) {
    if (*file) storage.close(*file);
    *file = nullptr;
  }
}

SyncOpSink SyncExecutor::sink() { return {this, &sinkOp, &sinkTotal}; }

void SyncExecutor::sinkTotal(void* ctx, uint32_t totalOps) { static_cast<SyncExecutor*>(ctx)->total = totalOps; }

bool SyncExecutor::sinkOp(void* ctx, const SyncOp& op) {
  auto* self = static_cast<SyncExecutor*>(ctx);
  if (self->cancelRequested) {
    LOG_INF(LOG_TAG, "Cancelled after %u of %u operations", static_cast<unsigned>(self->done),
            static_cast<unsigned>(self->total));
    self->cancelled = true;
    return false;
  }
  if (!self->startOp(op)) return false;
  ++self->done;
  if (self->callbacks.onProgress) {
    const char* path = self->treePath[0] != '\0' ? self->treePath : self->manifestPath;
    self->callbacks.onProgress(self->callbacks.ctx, {self->done, self->total, op.kind, path + 1});
  }
  return self->run(op) != SyncOpOutcome::Fatal;
}

SyncOpOutcome SyncExecutor::execute(const SyncOp& op) { return startOp(op) ? run(op) : SyncOpOutcome::Fatal; }

// Reads the operation's entries into the path buffers; false when the operation cannot run at all.
bool SyncExecutor::startOp(const SyncOp& op) {
  if (!manifestFile) {
    LOG_ERR(LOG_TAG, "Operation outside begin() and end()");
    return false;
  }
  if (failed) return false;
  if (!loadPaths(op)) {
    fatal("Cannot read the operation's entries", "");
    return false;
  }
  return true;
}

bool SyncExecutor::loadPaths(const SyncOp& op) {
  treePath[0] = '\0';
  manifestPath[0] = '\0';
  const bool needsTree = op.kind != SyncOpKind::Delete && op.kind != SyncOpKind::Prune;
  const bool needsManifest = op.kind == SyncOpKind::Move || op.kind == SyncOpKind::Delete ||
                             op.kind == SyncOpKind::Prune ||
                             (op.kind == SyncOpKind::Download && op.manifestOffset != NO_OFFSET);
  if (needsTree && !loadEntry(*treeReader, op.treeOffset, treePath, true)) return false;
  return !needsManifest || loadEntry(*manifestReader, op.manifestOffset, manifestPath, false);
}

// Copies the entry's path as an absolute path; keepEntry also keeps its SHA and size.
bool SyncExecutor::loadEntry(RecordReader& reader, uint32_t offset, char* absPath, bool keepEntry) {
  SyncRecord record;
  const RecordStatus status = reader.readEntryAt(offset, record);
  if (status != RecordStatus::Record) {
    LOG_ERR(LOG_TAG, "No entry at offset %u: status %u", static_cast<unsigned>(offset), static_cast<unsigned>(status));
    return false;
  }
  absPath[0] = '/';
  memcpy(absPath + 1, record.path, record.pathLen + 1);
  if (keepEntry) {
    memcpy(entrySha, record.sha, sizeof(entrySha));
    entrySize = record.size;
  }
  return true;
}

SyncOpOutcome SyncExecutor::run(const SyncOp& op) {
  if (op.kind != SyncOpKind::Skip && (!isSyncable(treePath) || !isSyncable(manifestPath))) {
    return fileError(SyncOpOutcome::RefusedPath);
  }
  switch (op.kind) {
    case SyncOpKind::Download:
      return download(op.manifestOffset);
    case SyncOpKind::AdoptOrDownload:
      return adoptOrDownload();
    case SyncOpKind::Delete:
      return remove(op.manifestOffset);
    case SyncOpKind::Prune:
      return prune(manifestPath);
    case SyncOpKind::Skip:
      return skip(op.reason);
    case SyncOpKind::Move:
      return move(op.manifestOffset);
  }
  return fatal("Unsupported operation for", treePath);
}

// Moves the manifest path to the tree path, or finishes a move whose file rename already happened.
// A resumed move verifies the tree path, and downloads it on a mismatch: either way it holds the same book.
SyncOpOutcome SyncExecutor::move(uint32_t replaces) {
  if (storage.exists(manifestPath) && !storage.exists(treePath)) {
    if (!makeParent(treePath) || !storage.rename(manifestPath, treePath)) {
      return fileError(SyncOpOutcome::TargetUnwritable);
    }
  } else if (!matchesTree()) {
    const SyncOpOutcome landed = land(NO_OFFSET);
    if (landed != SyncOpOutcome::Applied) return landed;
  }
  const SyncOpOutcome migrated = migrateState();
  if (migrated != SyncOpOutcome::Applied) return migrated;
  const SyncOpOutcome pruned = prune(manifestPath);
  if (pruned != SyncOpOutcome::Applied) return pruned;
  const SyncOpOutcome journaled = journalPut(replaces);
  if (journaled == SyncOpOutcome::Applied) ++result.moved;
  return journaled;
}

// Moves the manifest path's reading state to the tree path; a step already done is skipped.
// A cache already at the tree path belongs to an earlier book there: the hook clears it once, before any
// rename, since it may clear more than the kind that revealed it.
SyncOpOutcome SyncExecutor::migrateState() {
  for (const BookCacheKind kind : BOOK_CACHE_KINDS) {
    if (!formatCacheDirs(kind)) return fatal("Cannot derive the cache of", treePath);
    if (storage.exists(oldStatePath) && storage.exists(newStatePath)) {
      if (!clearCache(treePath)) return fatal("Cache clear failed", treePath);
      break;
    }
  }
  for (const BookCacheKind kind : BOOK_CACHE_KINDS) {
    if (!formatCacheDirs(kind)) return fatal("Cannot derive the cache of", treePath);
    const SyncOpOutcome moved = moveState();
    if (moved != SyncOpOutcome::Applied) return moved;
  }
  if (!formatBookmarkPath(oldStatePath, sizeof(oldStatePath), manifestPath) ||
      !formatBookmarkPath(newStatePath, sizeof(newStatePath), treePath)) {
    return fatal("Cannot derive the bookmark of", treePath);
  }
  const SyncOpOutcome moved = moveState();
  if (moved != SyncOpOutcome::Applied) return moved;
  if (callbacks.bookMoved && !callbacks.bookMoved(callbacks.ctx, manifestPath, treePath)) {
    return fatal("Move hook failed for", treePath);
  }
  return SyncOpOutcome::Applied;
}

bool SyncExecutor::formatCacheDirs(BookCacheKind kind) {
  return formatBookCacheDir(oldStatePath, sizeof(oldStatePath), BOOK_CACHE_ROOT, kind, manifestPath) &&
         formatBookCacheDir(newStatePath, sizeof(newStatePath), BOOK_CACHE_ROOT, kind, treePath);
}

// Renames oldStatePath to newStatePath, unless both name the same file on FAT (flattened bookmark names collide),
// it is gone already, or the destination holds its own state.
SyncOpOutcome SyncExecutor::moveState() {
  if (syncPathsEqualFolded(oldStatePath, strlen(oldStatePath), newStatePath, strlen(newStatePath))) {
    return SyncOpOutcome::Applied;
  }
  if (!storage.exists(oldStatePath)) return SyncOpOutcome::Applied;
  if (storage.exists(newStatePath)) {
    LOG_INF(LOG_TAG, "Reading state left in place, %s already exists", newStatePath);
    ++result.stateConflicts;
    return SyncOpOutcome::Applied;
  }
  if (!storage.rename(oldStatePath, newStatePath)) return fatal("Cannot move reading state", oldStatePath);
  return SyncOpOutcome::Applied;
}

// Creates absPath's parent directory when missing.
bool SyncExecutor::makeParent(char* absPath) {
  char* slash = strrchr(absPath, '/');
  if (slash == absPath) return true;
  *slash = '\0';
  const bool ready = storage.mkdirs(absPath);
  *slash = '/';
  return ready;
}

SyncOpOutcome SyncExecutor::download(uint32_t replaces) {
  const SyncOpOutcome landed = land(replaces);
  if (landed != SyncOpOutcome::Applied) return landed;
  const SyncOpOutcome journaled = journalPut(replaces);
  if (journaled != SyncOpOutcome::Applied) return journaled;
  if (replaces == NO_OFFSET) {
    ++result.added;
  } else {
    ++result.updated;
  }
  return SyncOpOutcome::Applied;
}

// Fetches and verifies the tree entry, then puts it in place of whatever is at the tree path.
SyncOpOutcome SyncExecutor::land(uint32_t replaces) {
  const SyncOpOutcome verified = fetchIntoTemp();
  if (verified != SyncOpOutcome::Applied) return verified;
  if (!makeParent(treePath)) return dropTemp(SyncOpOutcome::TargetUnwritable);
  if (replaces != NO_OFFSET && !clearCache(manifestPath)) return fatal("Cache clear failed", manifestPath);
  if ((replaces == NO_OFFSET || strcmp(manifestPath, treePath) != 0) && !clearCache(treePath)) {
    return fatal("Cache clear failed", treePath);
  }
  if (storage.exists(treePath)) {
    uint64_t size = 0;
    if (!storage.fileSize(treePath, size)) return dropTemp(SyncOpOutcome::TargetUnwritable);
    if (!storage.remove(treePath)) return fatal("Cannot remove", treePath);
  }
  if (!storage.rename(SYNC_DOWNLOAD_TMP_PATH, treePath)) return dropTemp(SyncOpOutcome::TargetUnwritable);
  return SyncOpOutcome::Applied;
}

SyncOpOutcome SyncExecutor::skip(SyncSkipReason reason) {
  LOG_INF(LOG_TAG, "Skipped (reason %u): %s", static_cast<unsigned>(reason), treePath);
  if (reason == SyncSkipReason::CaseCollision) ++result.skippedCaseCollision;
  if (reason == SyncSkipReason::UnsafeName) ++result.skippedUnsafeName;
  return SyncOpOutcome::Skipped;
}

// A missing file is already deleted: a crash may have come between the remove and the frame.
SyncOpOutcome SyncExecutor::remove(uint32_t manifestOffset) {
  if (storage.exists(manifestPath)) {
    uint64_t size = 0;
    if (!storage.fileSize(manifestPath, size)) return fileError(SyncOpOutcome::TargetUnwritable);
    if (!storage.remove(manifestPath)) return fatal("Cannot remove", manifestPath);
  }
  const SyncOpOutcome pruned = prune(manifestPath);
  if (pruned != SyncOpOutcome::Applied) return pruned;
  if (!journal->drop(manifestOffset)) return fatal("Cannot journal the delete of", manifestPath);
  const SyncOpOutcome journaled = journalFlush();
  if (journaled == SyncOpOutcome::Applied) ++result.deleted;
  return journaled;
}

// Removes absPath's empty ancestors, deepest first, up to the first one still holding something.
// A missing ancestor is passed over: a crash may have removed it already. Never the root.
// Walks in place, cutting absPath at each '/', and restores it before returning.
SyncOpOutcome SyncExecutor::prune(char* absPath) {
  const size_t len = strlen(absPath);
  SyncOpOutcome outcome = SyncOpOutcome::Applied;
  for (char* slash = strrchr(absPath, '/'); slash != absPath; slash = strrchr(absPath, '/')) {
    *slash = '\0';
    if (!storage.exists(absPath)) continue;
    if (!storage.isEmptyDirectory(absPath)) break;
    if (!storage.rmdir(absPath)) {
      outcome = fatal("Cannot remove the empty folder", absPath);
      break;
    }
    ++result.pruned;
  }
  for (size_t i = 0; i < len; ++i) {
    if (absPath[i] == '\0') absPath[i] = '/';
  }
  return outcome;
}

// A local file with the tree's size and SHA is journaled as is: no fetch, and its reading state stays.
SyncOpOutcome SyncExecutor::adoptOrDownload() {
  if (!matchesTree()) return download(NO_OFFSET);
  const SyncOpOutcome journaled = journalPut(NO_OFFSET);
  if (journaled == SyncOpOutcome::Applied) ++result.adopted;
  return journaled;
}

// Whether the file at the tree path has the tree entry's size and SHA; a size mismatch skips the hash.
bool SyncExecutor::matchesTree() {
  uint64_t localSize = 0;
  if (!callbacks.hashFile || !storage.fileSize(treePath, localSize) || localSize != entrySize) return false;
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  return callbacks.hashFile(callbacks.ctx, treePath, entrySize, sha) && memcmp(sha, entrySha, sizeof(sha)) == 0;
}

// Streams the tree entry into the download temp, hashing as it goes; Applied once it is verified.
SyncOpOutcome SyncExecutor::fetchIntoTemp() {
  tempFile = storage.open(SYNC_DOWNLOAD_TMP_PATH, SyncOpenMode::Truncate);
  if (!tempFile) return fatal("Cannot open", SYNC_DOWNLOAD_TMP_PATH);
  hasher.begin(entrySize);
  fetched = 0;
  tempWriteFailed = false;
  overflow = false;
  bool bodyOk = true;
  if (entrySize > 0) {
    const SyncFetchRequest request{entrySha, entrySize, treePath + 1};
    const SyncChunkSink chunkSink{this, &sinkWrite};
    bodyOk = callbacks.fetch && callbacks.fetch(callbacks.ctx, request, chunkSink);
  }
  const bool flushed = tempFile->flush();
  storage.close(tempFile);
  tempFile = nullptr;
  if (tempWriteFailed || !flushed) return fatal("Cannot write", SYNC_DOWNLOAD_TMP_PATH);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  const bool sized = hasher.finish(sha);
  if (!bodyOk && !overflow) {
    LOG_ERR(LOG_TAG, "Fetch failed: %s", treePath);
    return dropTemp(SyncOpOutcome::FetchFailed);
  }
  if (overflow || !sized || memcmp(sha, entrySha, sizeof(sha)) != 0) {
    LOG_ERR(LOG_TAG, "Verification failed: %s", treePath);
    return dropTemp(SyncOpOutcome::VerifyFailed);
  }
  return SyncOpOutcome::Applied;
}

bool SyncExecutor::sinkWrite(void* ctx, const uint8_t* data, size_t len) {
  auto* self = static_cast<SyncExecutor*>(ctx);
  if (self->overflow || self->tempWriteFailed) return false;
  if (len > self->entrySize - self->fetched) {
    self->overflow = true;
    return false;
  }
  self->hasher.update(data, len);
  self->fetched += len;
  if (!self->tempFile->write(data, len)) {
    self->tempWriteFailed = true;
    return false;
  }
  return true;
}

// Abandons the operation as a per-file error; the temp must go, or the card is failing.
SyncOpOutcome SyncExecutor::dropTemp(SyncOpOutcome outcome) {
  if (storage.exists(SYNC_DOWNLOAD_TMP_PATH) && !storage.remove(SYNC_DOWNLOAD_TMP_PATH)) {
    return fatal("Cannot remove", SYNC_DOWNLOAD_TMP_PATH);
  }
  return fileError(outcome);
}

SyncOpOutcome SyncExecutor::fileError(SyncOpOutcome outcome) {
  LOG_ERR(LOG_TAG, "Operation abandoned: outcome %u, %s", static_cast<unsigned>(outcome),
          treePath[0] != '\0' ? treePath : manifestPath);
  ++result.fileErrors;
  return outcome;
}

SyncOpOutcome SyncExecutor::fatal(const char* what, const char* path) {
  LOG_ERR(LOG_TAG, "%s %s: stopping the sync", what, path);
  failed = true;
  return SyncOpOutcome::Fatal;
}

bool SyncExecutor::clearCache(const char* absPath) {
  return !callbacks.clearBookCache || callbacks.clearBookCache(callbacks.ctx, absPath);
}

// The operation's commit point: one frame, flushed.
SyncOpOutcome SyncExecutor::journalPut(uint32_t replaces) {
  const char* path = treePath + 1;
  if (!journal->put(entrySha, entrySize, path, strlen(path), replaces, nullptr)) {
    return fatal("Cannot journal", path);
  }
  return journalFlush();
}

SyncOpOutcome SyncExecutor::journalFlush() {
  if (!manifestFile->flush()) return fatal("Cannot flush", SYNC_MANIFEST_PATH);
  return SyncOpOutcome::Applied;
}
