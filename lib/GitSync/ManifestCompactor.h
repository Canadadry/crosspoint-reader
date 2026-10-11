#pragma once

#include "RecordReader.h"
#include "RecordWriter.h"

// Truncated, Corrupt, WrongVersion and IoError pass the reader's status through.
// Corrupt also covers a target dropped or replaced twice, or naming no entry frame.
enum class CompactStatus : uint8_t { Ok, Truncated, Corrupt, WrongVersion, IoError, ScratchFull, WriteFailed };

struct CompactResult {
  CompactStatus status;
  // Ok only: entries in the new base, and whether a torn journal tail was left out.
  uint32_t liveCount;
  bool tornTailDropped;
};

// Folds a manifest's base and journal into a fresh sealed base, in file order, with no journal.
// Two passes over the source: the first gathers every drop and replaces target, the second writes
// each entry frame that no target names. The sink holds garbage unless the status is Ok; the caller
// writes it to a temporary file and replaces the manifest only on Ok.
// One compact() per compactor. About 640 bytes and no heap: hold it as a member or through
// makeUniqueNoThrow, never as a local.
class ManifestCompactor {
 public:
  ManifestCompactor(RecordSource& source, RecordSink& sink);

  ManifestCompactor(const ManifestCompactor&) = delete;
  ManifestCompactor& operator=(const ManifestCompactor&) = delete;

  // scratch holds one uint32_t per journal target, at most one per journal frame: ScratchFull past capacity.
  CompactResult compact(uint32_t* scratch, size_t capacity);

 private:
  CompactStatus collectTargets(uint32_t* scratch, size_t capacity, size_t& count);
  CompactStatus writeLive(const uint32_t* targets, size_t count, uint32_t& liveCount);

  RecordReader reader;
  RecordWriter writer;
};
