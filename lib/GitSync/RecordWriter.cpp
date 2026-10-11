#include "RecordWriter.h"

#include <cstring>

RecordWriter::RecordWriter(RecordSink& sink) : sink(sink) {}

bool RecordWriter::begin(RecordFileKind kind) {
  if (sinkFailed || stage != Stage::Unstarted) return false;
  memcpy(frame, RECORD_FILE_MAGIC, sizeof(RECORD_FILE_MAGIC));
  frame[4] = RECORD_FILE_VERSION;
  frame[5] = static_cast<uint8_t>(kind);
  frame[6] = 0;
  frame[7] = 0;
  if (!sink.write(frame, RECORD_HEADER_SIZE)) {
    sinkFailed = true;
    return false;
  }
  position = RECORD_HEADER_SIZE;
  this->kind = kind;
  stage = Stage::Entries;
  return true;
}

bool RecordWriter::appendTo(uint32_t endOffset) {
  if (sinkFailed || stage != Stage::Unstarted || endOffset < EMPTY_MANIFEST_SIZE) return false;
  position = endOffset;
  kind = RecordFileKind::Manifest;
  stage = Stage::Journal;
  return true;
}

bool RecordWriter::put(const uint8_t sha[GitBlobHasher::SHA_SIZE], uint64_t size, const char* path, size_t pathLen,
                       uint32_t replaces, uint32_t* offsetOut) {
  if (sinkFailed || (stage != Stage::Entries && stage != Stage::Journal)) return false;
  if (!sha || !path || pathLen == 0 || pathLen > MAX_SYNC_PATH_BYTES) return false;
  if (replaces != NO_OFFSET && (stage != Stage::Journal || !recordTargetPrecedes(replaces, position))) return false;
  uint8_t* body = frame + FRAME_HEADER_SIZE;
  memcpy(body, sha, GitBlobHasher::SHA_SIZE);
  recordStoreLe(body + ENTRY_SIZE_FIELD, size, 8);
  recordStoreLe(body + ENTRY_REPLACES_FIELD, replaces, 4);
  memcpy(body + ENTRY_FIXED_BODY_SIZE, path, pathLen);
  const uint32_t offset = position;
  if (!emit(RecordFrameKind::Entry, ENTRY_FIXED_BODY_SIZE + pathLen)) return false;
  ++entryCount;
  if (offsetOut) *offsetOut = offset;
  return true;
}

bool RecordWriter::seal() {
  if (sinkFailed || stage != Stage::Entries) return false;
  recordStoreLe(frame + FRAME_HEADER_SIZE, entryCount, OFFSET_BODY_SIZE);
  if (!emit(RecordFrameKind::Seal, OFFSET_BODY_SIZE)) return false;
  stage = kind == RecordFileKind::Manifest ? Stage::Journal : Stage::Closed;
  return true;
}

bool RecordWriter::drop(uint32_t target) {
  if (sinkFailed || stage != Stage::Journal || !recordTargetPrecedes(target, position)) return false;
  recordStoreLe(frame + FRAME_HEADER_SIZE, target, OFFSET_BODY_SIZE);
  return emit(RecordFrameKind::Drop, OFFSET_BODY_SIZE);
}

bool RecordWriter::emit(RecordFrameKind kind, size_t bodyLen) {
  frame[0] = static_cast<uint8_t>(kind);
  recordStoreLe(frame + 1, bodyLen, 2);
  recordStoreLe(frame + 3, recordFrameCrc(frame, bodyLen), 4);
  const size_t frameLen = FRAME_HEADER_SIZE + bodyLen;
  if (!sink.write(frame, frameLen)) {
    sinkFailed = true;
    return false;
  }
  position += frameLen;
  return true;
}
