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

// Record for an entry or a drop; End after a seal, to keep reading, or once finished.
RecordStatus RecordReader::readFrame(SyncRecord& record) {
  if (stage == Stage::Sealed) {
    const ReadResult result = readExactly(source, frame, 1);
    if (result == ReadResult::Error) return RecordStatus::IoError;
    return finish(result == ReadResult::Empty ? RecordStatus::End : RecordStatus::Corrupt);
  }
  const bool inJournal = stage == Stage::Journal;
  ReadResult result = readExactly(source, frame, FRAME_HEADER_SIZE);
  if (inJournal && result == ReadResult::Empty) return finish(RecordStatus::End);
  // A journal frame cut off by the end of the file is the torn tail of an interrupted append.
  if (inJournal && result == ReadResult::Short) return endTorn();
  if (result != ReadResult::Full) return incompleteStatus(result);
  const auto frameKind = static_cast<RecordFrameKind>(frame[0]);
  const size_t bodyLen = recordLoadLe(frame + 1, 2);
  if (!bodyLengthFits(frameKind, bodyLen)) return RecordStatus::Corrupt;
  result = readExactly(source, frame + FRAME_HEADER_SIZE, bodyLen);
  if (inJournal && (result == ReadResult::Short || result == ReadResult::Empty)) return endTorn();
  if (result != ReadResult::Full) return incompleteStatus(result);
  if (recordLoadLe(frame + 3, 4) != recordFrameCrc(frame, bodyLen)) {
    return inJournal ? tornIfLast() : RecordStatus::Corrupt;
  }
  const uint8_t* body = frame + FRAME_HEADER_SIZE;
  const uint32_t offset = position;
  position += FRAME_HEADER_SIZE + bodyLen;

  if (frameKind == RecordFrameKind::Seal) {
    if (inJournal || recordLoadLe(body, OFFSET_BODY_SIZE) != entryCount) return RecordStatus::Corrupt;
    stage = kind == RecordFileKind::Manifest ? Stage::Journal : Stage::Sealed;
    return RecordStatus::End;
  }
  record.kind = frameKind;
  record.offset = offset;
  record.inJournal = inJournal;
  if (frameKind == RecordFrameKind::Drop) {
    const auto target = static_cast<uint32_t>(recordLoadLe(body, OFFSET_BODY_SIZE));
    if (!inJournal || !recordTargetPrecedes(target, offset)) return RecordStatus::Corrupt;
    record.target = target;
    record.replaces = NO_OFFSET;
    record.path = nullptr;
    record.pathLen = 0;
    record.size = 0;
    return RecordStatus::Record;
  }

  const auto replaces = static_cast<uint32_t>(recordLoadLe(body + ENTRY_REPLACES_FIELD, 4));
  if (replaces != NO_OFFSET && (!inJournal || !recordTargetPrecedes(replaces, offset))) return RecordStatus::Corrupt;
  decodeEntry(offset, bodyLen, record);
  record.inJournal = inJournal;
  if (!inJournal) ++entryCount;
  return RecordStatus::Record;
}

RecordStatus RecordReader::readEntryAt(uint32_t offset, SyncRecord& record) {
  if (offset < RECORD_HEADER_SIZE) return RecordStatus::Corrupt;
  if (!source.seek(offset)) return RecordStatus::IoError;
  ReadResult result = readExactly(source, frame, FRAME_HEADER_SIZE);
  if (result != ReadResult::Full) return incompleteStatus(result);
  const size_t bodyLen = recordLoadLe(frame + 1, 2);
  if (static_cast<RecordFrameKind>(frame[0]) != RecordFrameKind::Entry ||
      !bodyLengthFits(RecordFrameKind::Entry, bodyLen)) {
    return RecordStatus::Corrupt;
  }
  result = readExactly(source, frame + FRAME_HEADER_SIZE, bodyLen);
  if (result != ReadResult::Full) return incompleteStatus(result);
  if (recordLoadLe(frame + 3, 4) != recordFrameCrc(frame, bodyLen)) return RecordStatus::Corrupt;
  decodeEntry(offset, bodyLen, record);
  if (record.replaces != NO_OFFSET && !recordTargetPrecedes(record.replaces, offset)) return RecordStatus::Corrupt;
  record.inJournal = false;
  return RecordStatus::Record;
}

void RecordReader::decodeEntry(uint32_t offset, size_t bodyLen, SyncRecord& record) {
  const uint8_t* body = frame + FRAME_HEADER_SIZE;
  record.kind = RecordFrameKind::Entry;
  record.offset = offset;
  memcpy(record.sha, body, GitBlobHasher::SHA_SIZE);
  record.size = recordLoadLe(body + ENTRY_SIZE_FIELD, 8);
  record.replaces = static_cast<uint32_t>(recordLoadLe(body + ENTRY_REPLACES_FIELD, 4));
  record.pathLen = bodyLen - ENTRY_FIXED_BODY_SIZE;
  frame[FRAME_HEADER_SIZE + bodyLen] = '\0';
  record.path = reinterpret_cast<const char*>(body + ENTRY_FIXED_BODY_SIZE);
  record.target = NO_OFFSET;
}

RecordStatus RecordReader::endTorn() {
  torn = true;
  return finish(RecordStatus::End);
}

// A journal frame failing its CRC is the torn tail only when nothing follows it.
RecordStatus RecordReader::tornIfLast() {
  const ReadResult result = readExactly(source, frame, 1);
  if (result == ReadResult::Error) return RecordStatus::IoError;
  return result == ReadResult::Empty ? endTorn() : RecordStatus::Corrupt;
}
