#include "TreeSnapshotWriter.h"

#include <Logging.h>

#include "SyncPath.h"
#include "SyncPlanner.h"

namespace {
constexpr const char* LOG_TAG = "GITSNAP";
}  // namespace

TreeSnapshotWriter::TreeSnapshotWriter(RecordSink& sink) : writer(sink), parser(this, &TreeSnapshotWriter::sOnRecord) {}

bool TreeSnapshotWriter::begin() { return writer.begin(RecordFileKind::Tree); }

void TreeSnapshotWriter::feed(const char* data, size_t len) {
  if (cancelled) return;
  parser.feed(data, len);
}

// The cap and a refused write abort the parser, so wasAborted() covers them.
bool TreeSnapshotWriter::stopped() const { return cancelled || parser.wasAborted() || parser.hasError(); }

void TreeSnapshotWriter::cancel() { cancelled = true; }

TreeSnapshotStatus TreeSnapshotWriter::finish() {
  if (writer.failed()) {
    LOG_ERR(LOG_TAG, "Tree file write failed after %u entries", static_cast<unsigned>(written));
    return TreeSnapshotStatus::WriteFailed;
  }
  if (overCap) {
    LOG_ERR(LOG_TAG, "Tree refused: more than %u syncable files", static_cast<unsigned>(MAX_SYNC_FILES));
    return TreeSnapshotStatus::TooManyFiles;
  }
  if (cancelled) {
    LOG_ERR(LOG_TAG, "Tree download cancelled after %u entries", static_cast<unsigned>(written));
    return TreeSnapshotStatus::Cancelled;
  }
  if (!parser.isComplete()) {
    const char* reason = parser.wasAborted() ? "entry refused by the record writer"
                         : parser.hasError() ? "parse error"
                                             : "body cut off";
    LOG_ERR(LOG_TAG, "Invalid tree: %s after %u blobs (%u written)", reason,
            static_cast<unsigned>(parser.emittedCount()), static_cast<unsigned>(written));
    return TreeSnapshotStatus::Invalid;
  }
  if (parser.isTruncated()) {
    LOG_ERR(LOG_TAG, "Tree truncated by GitHub after %u blobs", static_cast<unsigned>(parser.emittedCount()));
    return TreeSnapshotStatus::Truncated;
  }
  if (!writer.seal()) {
    LOG_ERR(LOG_TAG, "Tree file seal failed after %u entries", static_cast<unsigned>(written));
    return TreeSnapshotStatus::WriteFailed;
  }
  return TreeSnapshotStatus::Ok;
}

bool TreeSnapshotWriter::sOnRecord(void* ctx, const GitTreeRecord& record) {
  return static_cast<TreeSnapshotWriter*>(ctx)->onRecord(record);
}

// The path may hold control bytes or exceed any log line, so only the SHA is printed.
void TreeSnapshotWriter::skipBySha(const GitTreeRecord& record, const char* reason, size_t& counter) {
  counter++;
  char hex[SHA_HEX_LENGTH + 1];
  shaToHex(record.sha, hex);
  LOG_ERR(LOG_TAG, "Skipped blob %s: %s path (%u bytes)", hex, reason, static_cast<unsigned>(record.pathLen));
}

bool TreeSnapshotWriter::onRecord(const GitTreeRecord& record) {
  switch (classifySyncPath(record.path, record.pathLen)) {
    case SyncPathVerdict::Syncable:
      if (written == MAX_SYNC_FILES) {
        overCap = true;
        return false;
      }
      break;
    case SyncPathVerdict::Hidden:
      hidden++;
      return true;
    case SyncPathVerdict::Malformed:
      skipBySha(record, "malformed", malformed);
      return true;
    case SyncPathVerdict::TooLong:
      skipBySha(record, "too long", tooLong);
      return true;
  }
  if (!writer.put(record.sha, record.size, record.path, record.pathLen, NO_OFFSET, nullptr)) return false;
  written++;
  return true;
}
