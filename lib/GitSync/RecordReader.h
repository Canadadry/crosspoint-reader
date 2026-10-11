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

  // Seeks the source to the file's first byte and reads the file afresh; false when the seek is refused.
  bool rewind();

  // Reads the entry frame at offset, checked against its CRC but not against the rest of the file:
  // the caller already read that file to End. inJournal reads false. Corrupt for an offset that is not
  // an entry frame, IoError when the source refuses the seek. next() must not be called afterwards.
  RecordStatus readEntryAt(uint32_t offset, SyncRecord& record);

  // After End: a cut-off or CRC-failing last journal frame was ignored, as if its append never happened.
  bool tornTail() const { return torn; }
  // After End: the file size up to the last valid frame, where a journal append resumes.
  uint32_t endOffset() const { return position; }

 private:
  enum class Stage : uint8_t { Header, Entries, Sealed, Journal, Done };

  RecordStatus readHeader();
  RecordStatus readFrame(SyncRecord& record);
  RecordStatus finish(RecordStatus status);
  RecordStatus endTorn();
  RecordStatus tornIfLast();
  void decodeEntry(uint32_t offset, size_t bodyLen, SyncRecord& record);

  RecordSource& source;
  RecordFileKind kind;
  Stage stage = Stage::Header;
  RecordStatus doneStatus = RecordStatus::End;
  uint32_t position = 0;
  uint32_t entryCount = 0;
  bool torn = false;
  // One byte past the largest frame terminates the path in place.
  uint8_t frame[MAX_FRAME_SIZE + 1];
};
