#pragma once

#include "RecordFormat.h"
#include "RecordStream.h"

// Reads a record file frame by frame; the source must be at the file's first byte.
// Never half-applied: records returned before a status other than End must be discarded.
// About 320 bytes and no heap: hold it as a member or through makeUniqueNoThrow, never as a local.
class RecordReader {
 public:
  RecordReader(RecordSource& source, RecordFileKind kind);

  RecordReader(const RecordReader&) = delete;
  RecordReader& operator=(const RecordReader&) = delete;

  // Record fills record; any other status repeats on every later call.
  RecordStatus next(SyncRecord& record);

 private:
  enum class Stage : uint8_t { Header, Entries, Sealed, Done };

  RecordStatus readHeader();
  RecordStatus readFrame(SyncRecord& record);
  RecordStatus finish(RecordStatus status);

  RecordSource& source;
  RecordFileKind kind;
  Stage stage = Stage::Header;
  RecordStatus doneStatus = RecordStatus::End;
  uint32_t position = 0;
  uint32_t entryCount = 0;
  // One byte past the largest frame terminates the path in place.
  uint8_t frame[MAX_FRAME_SIZE + 1];
};
