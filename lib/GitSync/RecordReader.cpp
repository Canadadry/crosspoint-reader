#include "RecordReader.h"

#include <cstring>

namespace {

enum class ReadResult : uint8_t { Full, Empty, Short, Error };

// Loops because a source may return fewer bytes than asked before its end.
ReadResult readExactly(RecordSource& source, uint8_t* buf, size_t len) {
  size_t done = 0;
  while (done < len) {
    const int got = source.read(buf + done, len - done);
    if (got < 0) return ReadResult::Error;
    if (got == 0) return done == 0 ? ReadResult::Empty : ReadResult::Short;
    done += static_cast<size_t>(got);
  }
  return ReadResult::Full;
}

// Also rejects an unknown frame kind, so the body always fits the frame buffer.
bool bodyLengthFits(RecordFrameKind kind, size_t bodyLen) {
  switch (kind) {
    case RecordFrameKind::Entry:
      return bodyLen > ENTRY_FIXED_BODY_SIZE && bodyLen <= ENTRY_FIXED_BODY_SIZE + MAX_SYNC_PATH_BYTES;
    case RecordFrameKind::Drop:
    case RecordFrameKind::Seal:
      return bodyLen == OFFSET_BODY_SIZE;
  }
  return false;
}

RecordStatus incompleteStatus(ReadResult result) {
  return result == ReadResult::Error ? RecordStatus::IoError : RecordStatus::Truncated;
}

}  // namespace

RecordReader::RecordReader(RecordSource& source, RecordFileKind kind) : source(source), kind(kind) {}

RecordStatus RecordReader::next(SyncRecord& record) {
  if (stage == Stage::Header) {
    const RecordStatus status = readHeader();
    if (status != RecordStatus::Record) return finish(status);
  }
  while (stage != Stage::Done) {
    const RecordStatus status = readFrame(record);
    if (status == RecordStatus::Record) return status;
    if (status != RecordStatus::End) return finish(status);
  }
  return doneStatus;
}

RecordStatus RecordReader::finish(RecordStatus status) {
  stage = Stage::Done;
  doneStatus = status;
  return status;
}

RecordStatus RecordReader::readHeader() {
  const ReadResult result = readExactly(source, frame, RECORD_HEADER_SIZE);
  if (result != ReadResult::Full) return incompleteStatus(result);
  if (memcmp(frame, RECORD_FILE_MAGIC, sizeof(RECORD_FILE_MAGIC)) != 0) return RecordStatus::Corrupt;
  if (frame[4] != RECORD_FILE_VERSION) return RecordStatus::WrongVersion;
  if (frame[5] != static_cast<uint8_t>(kind) || frame[6] != 0 || frame[7] != 0) return RecordStatus::Corrupt;
  position = RECORD_HEADER_SIZE;
  stage = Stage::Entries;
  return RecordStatus::Record;
}

// Record for an entry; End after the seal is read or at a clean end of file after it.
RecordStatus RecordReader::readFrame(SyncRecord& record) {
  if (stage == Stage::Sealed) {
    const ReadResult result = readExactly(source, frame, 1);
    if (result == ReadResult::Error) return RecordStatus::IoError;
    return finish(result == ReadResult::Empty ? RecordStatus::End : RecordStatus::Corrupt);
  }
  ReadResult result = readExactly(source, frame, FRAME_HEADER_SIZE);
  if (result != ReadResult::Full) return incompleteStatus(result);
  const auto frameKind = static_cast<RecordFrameKind>(frame[0]);
  const size_t bodyLen = recordLoadLe(frame + 1, 2);
  if (!bodyLengthFits(frameKind, bodyLen)) return RecordStatus::Corrupt;
  result = readExactly(source, frame + FRAME_HEADER_SIZE, bodyLen);
  if (result != ReadResult::Full) return incompleteStatus(result);
  if (recordLoadLe(frame + 3, 4) != recordFrameCrc(frame, bodyLen)) return RecordStatus::Corrupt;
  const uint8_t* body = frame + FRAME_HEADER_SIZE;
  const uint32_t offset = position;
  position += FRAME_HEADER_SIZE + bodyLen;

  if (frameKind == RecordFrameKind::Seal) {
    if (recordLoadLe(body, OFFSET_BODY_SIZE) != entryCount) return RecordStatus::Corrupt;
    stage = Stage::Sealed;
    return RecordStatus::End;
  }
  if (frameKind == RecordFrameKind::Drop) return RecordStatus::Corrupt;

  const auto replaces = static_cast<uint32_t>(recordLoadLe(body + ENTRY_REPLACES_FIELD, 4));
  if (replaces != NO_OFFSET) return RecordStatus::Corrupt;
  record.kind = RecordFrameKind::Entry;
  record.offset = offset;
  record.inJournal = false;
  memcpy(record.sha, body, GitBlobHasher::SHA_SIZE);
  record.size = recordLoadLe(body + ENTRY_SIZE_FIELD, 8);
  record.replaces = replaces;
  record.pathLen = bodyLen - ENTRY_FIXED_BODY_SIZE;
  frame[FRAME_HEADER_SIZE + bodyLen] = '\0';
  record.path = reinterpret_cast<const char*>(body + ENTRY_FIXED_BODY_SIZE);
  record.target = NO_OFFSET;
  ++entryCount;
  return RecordStatus::Record;
}
