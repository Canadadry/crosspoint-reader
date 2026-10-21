#include "ManifestCompactor.h"

#include <algorithm>

namespace {

CompactStatus fromReader(RecordStatus status) {
  switch (status) {
    case RecordStatus::Truncated:
      return CompactStatus::Truncated;
    case RecordStatus::WrongVersion:
      return CompactStatus::WrongVersion;
    case RecordStatus::IoError:
      return CompactStatus::IoError;
    case RecordStatus::Record:
    case RecordStatus::End:
    case RecordStatus::Corrupt:
      break;
  }
  return CompactStatus::Corrupt;
}

}  // namespace

ManifestCompactor::ManifestCompactor(RecordSource& source, RecordSink& sink)
    : reader(source, RecordFileKind::Manifest), writer(sink) {}

CompactResult ManifestCompactor::compact(uint32_t* scratch, size_t capacity) {
  CompactResult result{CompactStatus::Ok, 0, false};
  if (!scratch) capacity = 0;
  size_t count = 0;
  result.status = collectTargets(scratch, capacity, count);
  if (result.status != CompactStatus::Ok) return result;
  if (count > capacity) {
    result.status = CompactStatus::ScratchFull;
    return result;
  }
  result.tornTailDropped = reader.tornTail();
  std::sort(scratch, scratch + count);
  if (std::adjacent_find(scratch, scratch + count) != scratch + count) {
    result.status = CompactStatus::Corrupt;
    return result;
  }
  if (!reader.rewind()) {
    result.status = CompactStatus::IoError;
    return result;
  }
  result.status = writeLive(scratch, count, result.liveCount);
  return result;
}

CompactStatus ManifestCompactor::collectTargets(uint32_t* scratch, size_t capacity, size_t& count) {
  SyncRecord record;
  RecordStatus status;
  while ((status = reader.next(record)) == RecordStatus::Record) {
    const uint32_t target = record.kind == RecordFrameKind::Drop ? record.target : record.replaces;
    if (target == NO_OFFSET) continue;
    // Counts on past capacity so that a damaged file still reports its own status first.
    if (count < capacity) scratch[count] = target;
    ++count;
  }
  return status == RecordStatus::End ? CompactStatus::Ok : fromReader(status);
}

CompactStatus ManifestCompactor::writeLive(const uint32_t* targets, size_t count, uint32_t& liveCount) {
  if (!writer.begin(RecordFileKind::Manifest)) return CompactStatus::WriteFailed;
  SyncRecord record;
  RecordStatus status;
  size_t matched = 0;
  while ((status = reader.next(record)) == RecordStatus::Record) {
    if (record.kind != RecordFrameKind::Entry) continue;
    if (std::binary_search(targets, targets + count, record.offset)) {
      ++matched;
      continue;
    }
    if (!writer.put(record.sha, record.size, record.path, record.pathLen, NO_OFFSET, nullptr)) {
      return CompactStatus::WriteFailed;
    }
    ++liveCount;
  }
  if (status != RecordStatus::End) return fromReader(status);
  // A target that matched no entry frame names a seal, a drop or the inside of a frame.
  if (matched != count) return CompactStatus::Corrupt;
  return writer.seal() ? CompactStatus::Ok : CompactStatus::WriteFailed;
}
