#pragma once

#include <cstddef>
#include <cstdint>

#include "GitTreeParser.h"
#include "RecordStream.h"
#include "RecordWriter.h"
#include "SyncBudget.h"

enum class TreeSnapshotStatus : uint8_t {
  Ok,
  // `truncated: true`: GitHub left entries out.
  Truncated,
  // Parser error, a body that did not end, or an entry the record writer refused.
  Invalid,
  // More than MAX_SYNC_FILES syncable blobs.
  TooManyFiles,
  // A sink write failed.
  WriteFailed,
  // cancel() was called.
  Cancelled,
};

// Streams a trees response into a tree record file: syncable blobs only, in body order, sealed only
// when the body is complete and not truncated.
// About 1.6 KB and no heap: hold it through makeUniqueNoThrow for the tree phase only, never as a local.
class TreeSnapshotWriter {
 public:
  explicit TreeSnapshotWriter(RecordSink& sink);

  TreeSnapshotWriter(const TreeSnapshotWriter&) = delete;
  TreeSnapshotWriter& operator=(const TreeSnapshotWriter&) = delete;

  // Writes the tree file header.
  bool begin();
  // Ignores every byte once stopped().
  void feed(const char* data, size_t len);
  // The transfer must end: the tree was refused (cap, parse error, failed write) or cancel() was called.
  // False on a complete body, so its last chunk is never answered with a stop.
  bool stopped() const;
  void cancel();
  // Seals the file when the snapshot is usable. Precedence: WriteFailed, TooManyFiles, Cancelled, Invalid,
  // Truncated, Ok. Call once, after the transfer.
  TreeSnapshotStatus finish();

  size_t entriesWritten() const { return written; }
  // Dot paths, skipped quietly.
  size_t hiddenCount() const { return hidden; }
  size_t malformedCount() const { return malformed; }
  size_t tooLongCount() const { return tooLong; }
  size_t skippedLongPathCount() const { return parser.skippedLongPathCount(); }
  size_t skippedEscapedPathCount() const { return parser.skippedEscapedPathCount(); }
  // Meaningful only after finish() returned Ok.
  const uint8_t* treeSha() const { return parser.treeSha(); }

 private:
  static bool sOnRecord(void* ctx, const GitTreeRecord& record);
  bool onRecord(const GitTreeRecord& record);
  static void skipBySha(const GitTreeRecord& record, const char* reason, size_t& counter);

  RecordWriter writer;
  GitTreeParser parser;
  size_t written = 0;
  size_t hidden = 0;
  size_t malformed = 0;
  size_t tooLong = 0;
  bool overCap = false;
  bool cancelled = false;
};

static_assert(sizeof(TreeSnapshotWriter) <= SYNC_TREE_WRITER_SHARE_BYTES,
              "TreeSnapshotWriter outgrew its share of SYNC_HEAP_BUDGET_BYTES");
