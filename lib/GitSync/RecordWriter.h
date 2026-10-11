#pragma once

#include "RecordFormat.h"
#include "RecordStream.h"

// Writes a record file through a sink, one sink write per frame so a tear cuts at most that frame.
// About 310 bytes and no heap: hold it as a member or through makeUniqueNoThrow, never as a local.
class RecordWriter {
 public:
  explicit RecordWriter(RecordSink& sink);

  RecordWriter(const RecordWriter&) = delete;
  RecordWriter& operator=(const RecordWriter&) = delete;

  // A refused call (bad argument, or out of the file grammar) writes nothing and returns false.
  bool begin(RecordFileKind kind);
  // Continues a manifest's journal; the sink must be positioned at endOffset, the reader's endOffset() after End.
  bool appendTo(uint32_t endOffset);
  // Writes one entry frame; offsetOut, when not null, receives its offset.
  // pathLen must be 1..MAX_SYNC_PATH_BYTES; the path is not filtered. replaces is NO_OFFSET, or in a manifest
  // journal an earlier entry's offset: a modify or a move persisted as one frame.
  bool put(const uint8_t sha[GitBlobHasher::SHA_SIZE], uint64_t size, const char* path, size_t pathLen,
           uint32_t replaces, uint32_t* offsetOut);
  // Writes one drop frame for an earlier entry's offset (manifest journal only).
  bool drop(uint32_t target);
  // Writes the seal with the number of entries put since begin(). A manifest then continues in its journal.
  bool seal();

  // A sink write failed: every later call returns false.
  bool failed() const { return sinkFailed; }

 private:
  enum class Stage : uint8_t { Unstarted, Entries, Journal, Closed };

  bool emit(RecordFrameKind kind, size_t bodyLen);

  RecordSink& sink;
  RecordFileKind kind = RecordFileKind::Tree;
  Stage stage = Stage::Unstarted;
  uint32_t position = 0;
  uint32_t entryCount = 0;
  bool sinkFailed = false;
  uint8_t frame[MAX_FRAME_SIZE];
};
