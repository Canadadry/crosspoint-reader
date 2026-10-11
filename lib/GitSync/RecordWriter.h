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
  // Writes one entry frame; offsetOut, when not null, receives its offset.
  // pathLen must be 1..MAX_SYNC_PATH_BYTES; the path is not filtered. replaces must be NO_OFFSET.
  bool put(const uint8_t sha[GitBlobHasher::SHA_SIZE], uint64_t size, const char* path, size_t pathLen,
           uint32_t replaces, uint32_t* offsetOut);
  // Writes the seal with the number of entries put since begin().
  bool seal();

  // A sink write failed: every later call returns false.
  bool failed() const { return sinkFailed; }

 private:
  enum class Stage : uint8_t { Unstarted, Entries, Closed };

  bool emit(RecordFrameKind kind, size_t bodyLen);

  RecordSink& sink;
  Stage stage = Stage::Unstarted;
  uint32_t position = 0;
  uint32_t entryCount = 0;
  bool sinkFailed = false;
  uint8_t frame[MAX_FRAME_SIZE];
};
