#include "SyncPlanner.h"

#include <algorithm>
#include <cstring>

#include "SyncPath.h"
#include "SyncPathFold.h"

namespace {

constexpr uint32_t OFFSET_MASK = 0x00FFFFFFu;
constexpr uint32_t STATE_SHIFT = 28;
constexpr uint32_t STATE_MASK = 0xFu << STATE_SHIFT;
constexpr uint32_t FLAG_NESTED = 1u << 24;
constexpr uint32_t FLAG_TARGET_EXISTS = 1u << 25;

enum SlotState : uint32_t {
  MANIFEST_MISSING,
  MANIFEST_UNCHANGED,
  MANIFEST_PATH_MATCHED,
  REMOTE_NEW,
  // A case-only rename: same SHA at a path equal after folding, nothing to do.
  REMOTE_UNCHANGED,
  // A modify: shaPrefix holds the replaced manifest offset.
  REMOTE_PATH_MATCHED,
};

uint32_t slotOffset(uint32_t offsetAndState) { return offsetAndState & OFFSET_MASK; }
uint32_t slotState(uint32_t offsetAndState) { return offsetAndState >> STATE_SHIFT; }
uint32_t withState(uint32_t offsetAndState, uint32_t state) {
  return (offsetAndState & ~STATE_MASK) | (state << STATE_SHIFT);
}

// Emission order; slots in GROUP_NONE emit nothing.
// GROUP_REMOVED emits its deletes, then its prunes.
enum EmitGroup : uint32_t { GROUP_ADOPT, GROUP_DOWNLOAD, GROUP_REMOVED, GROUP_NONE };

EmitGroup emitGroup(uint32_t offsetAndState) {
  switch (slotState(offsetAndState)) {
    case MANIFEST_MISSING:
      return GROUP_REMOVED;
    case REMOTE_NEW:
      return (offsetAndState & FLAG_TARGET_EXISTS) ? GROUP_ADOPT : GROUP_DOWNLOAD;
    case REMOTE_PATH_MATCHED:
      return GROUP_DOWNLOAD;
    default:
      return GROUP_NONE;
  }
}

uint32_t shaPrefixOf(const uint8_t* sha) {
  return (static_cast<uint32_t>(sha[0]) << 24) | (static_cast<uint32_t>(sha[1]) << 16) |
         (static_cast<uint32_t>(sha[2]) << 8) | sha[3];
}

uint32_t packOffsetAndState(const SyncRecord& record, SlotState state) {
  const bool nested = memchr(record.path, '/', record.pathLen) != nullptr;
  return record.offset | (nested ? FLAG_NESTED : 0) | (static_cast<uint32_t>(state) << STATE_SHIFT);
}

// Manifest slots are searched in this order while the tree streams.
constexpr auto BY_SHA_PREFIX_THEN_KEY = [](const auto& a, const auto& b) {
  return a.shaPrefix != b.shaPrefix ? a.shaPrefix < b.shaPrefix : a.pathKey < b.pathKey;
};

uint32_t totalOps(const SyncPlanResult& result) {
  return result.moves + result.adoptOrDownloads + result.downloads + result.deletes + result.prunes +
         result.caseCollisionSkips + result.unsafeNameSkips;
}

bool refuse(SyncPlanResult& result, SyncPlanStatus status, RecordStatus readerStatus) {
  result.status = status;
  result.readerStatus = readerStatus;
  return false;
}

// Counts an entry outside the sync's scope; true for a syncable one.
bool admit(const SyncRecord& record, SyncPlanResult& result) {
  switch (classifySyncPath(record.path, record.pathLen)) {
    case SyncPathVerdict::Syncable:
      return true;
    case SyncPathVerdict::Hidden:
      ++result.hiddenIgnored;
      return false;
    case SyncPathVerdict::Malformed:
    case SyncPathVerdict::TooLong:
      break;
  }
  ++result.malformedIgnored;
  return false;
}

}  // namespace

SyncPlanResult SyncPlanner::plan(RecordSource& manifest, RecordSource& tree, SyncLocalExistsFn localExists,
                                 void* existsCtx, const SyncOpSink& sink) {
  SyncPlanResult result;
  manifestSlots = 0;
  slotCount = 0;
  overCap = false;
  if (!loadManifest(manifest, result) || !streamTree(tree, result)) return result;
  if (overCap) {
    result.status = SyncPlanStatus::CapExceeded;
    return result;
  }
  if (!matchPaths(result) || !checkLocal(localExists, existsCtx, result)) return result;
  countOps(result);
  if (sink.onTotal) sink.onTotal(sink.ctx, totalOps(result));
  if (!emit(sink)) result.status = SyncPlanStatus::Aborted;
  return result;
}

bool SyncPlanner::loadManifest(RecordSource& source, SyncPlanResult& result) {
  manifestReader.emplace(source, RecordFileKind::Manifest);
  SyncRecord record;
  RecordStatus status;
  while ((status = manifestReader->next(record)) == RecordStatus::Record) {
    if (record.inJournal || record.kind != RecordFrameKind::Entry) {
      return refuse(result, SyncPlanStatus::ManifestNotCompacted, RecordStatus::End);
    }
    if (!admit(record, result)) continue;
    ++result.manifestEntries;
    if (result.manifestEntries > MAX_SYNC_FILES || record.offset > OFFSET_MASK) {
      overCap = true;
      continue;
    }
    slots[slotCount++] = Slot{shaPrefixOf(record.sha), syncPathKey(record.path, record.pathLen),
                              packOffsetAndState(record, MANIFEST_MISSING)};
  }
  if (status != RecordStatus::End) return refuse(result, SyncPlanStatus::ManifestError, status);
  manifestSlots = slotCount;
  std::sort(slots, slots + manifestSlots, BY_SHA_PREFIX_THEN_KEY);
  return true;
}

bool SyncPlanner::streamTree(RecordSource& source, SyncPlanResult& result) {
  treeReader.emplace(source, RecordFileKind::Tree);
  SyncRecord record;
  RecordStatus status;
  while ((status = treeReader->next(record)) == RecordStatus::Record) {
    if (!admit(record, result)) continue;
    ++result.treeEntries;
    if (overCap || result.treeEntries > MAX_SYNC_FILES || record.offset > OFFSET_MASK) {
      overCap = true;
      continue;
    }
    const Slot remote{shaPrefixOf(record.sha), syncPathKey(record.path, record.pathLen),
                      packOffsetAndState(record, REMOTE_NEW)};
    bool unchanged = false;
    if (!findUnchanged(remote, record, unchanged, result)) return false;
    if (!unchanged) slots[slotCount++] = remote;
  }
  if (status != RecordStatus::End) return refuse(result, SyncPlanStatus::TreeError, status);
  return true;
}

// Marks the manifest slot holding this exact entry, same full SHA and byte-identical path, as unchanged.
bool SyncPlanner::findUnchanged(const Slot& remote, const SyncRecord& entry, bool& unchanged, SyncPlanResult& result) {
  const auto [first, last] = std::equal_range(slots, slots + manifestSlots, remote, BY_SHA_PREFIX_THEN_KEY);
  for (Slot* slot = first; slot != last; ++slot) {
    if (slotState(slot->offsetAndState) != MANIFEST_MISSING) continue;
    SyncRecord stored;
    if (!readManifestAt(slotOffset(slot->offsetAndState), stored, result)) return false;
    if (stored.pathLen == entry.pathLen && memcmp(stored.path, entry.path, entry.pathLen) == 0 &&
        memcmp(stored.sha, entry.sha, GitBlobHasher::SHA_SIZE) == 0) {
      slot->offsetAndState = withState(slot->offsetAndState, MANIFEST_UNCHANGED);
      unchanged = true;
      return true;
    }
  }
  return true;
}

// Pairs each new remote slot with the missing manifest slot whose path is equal after folding.
bool SyncPlanner::matchPaths(SyncPlanResult& result) {
  std::sort(slots, slots + slotCount, [](const Slot& a, const Slot& b) {
    return a.pathKey != b.pathKey ? a.pathKey < b.pathKey : a.offsetAndState < b.offsetAndState;
  });
  size_t runEnd = 0;
  for (size_t runBegin = 0; runBegin < slotCount; runBegin = runEnd) {
    runEnd = runBegin + 1;
    while (runEnd < slotCount && slots[runEnd].pathKey == slots[runBegin].pathKey) ++runEnd;
    if (runEnd - runBegin < 2) continue;
    for (size_t r = runBegin; r < runEnd; ++r) {
      if (slotState(slots[r].offsetAndState) == REMOTE_NEW && !matchRemote(slots[r], runBegin, runEnd, result)) {
        return false;
      }
    }
  }
  return true;
}

bool SyncPlanner::matchRemote(Slot& remote, size_t runBegin, size_t runEnd, SyncPlanResult& result) {
  // The remote path is read into the scratch only once a candidate needs it.
  size_t remoteLen = 0;
  for (size_t m = runBegin; m < runEnd; ++m) {
    Slot& stored = slots[m];
    if (slotState(stored.offsetAndState) != MANIFEST_MISSING) continue;
    if (remoteLen == 0) {
      SyncRecord entry;
      if (!readTreeAt(slotOffset(remote.offsetAndState), entry, result)) return false;
      memcpy(scratchPath, entry.path, entry.pathLen + 1);
      memcpy(scratchSha, entry.sha, GitBlobHasher::SHA_SIZE);
      remoteLen = entry.pathLen;
    }
    SyncRecord record;
    if (!readManifestAt(slotOffset(stored.offsetAndState), record, result)) return false;
    if (!syncPathsEqualFolded(scratchPath, remoteLen, record.path, record.pathLen)) continue;
    stored.offsetAndState = withState(stored.offsetAndState, MANIFEST_PATH_MATCHED);
    if (memcmp(scratchSha, record.sha, GitBlobHasher::SHA_SIZE) == 0) {
      remote.offsetAndState = withState(remote.offsetAndState, REMOTE_UNCHANGED);
    } else {
      remote.offsetAndState = withState(remote.offsetAndState, REMOTE_PATH_MATCHED);
      remote.shaPrefix = record.offset;
    }
    return true;
  }
  return true;
}

bool SyncPlanner::readManifestAt(uint32_t offset, SyncRecord& record, SyncPlanResult& result) {
  const RecordStatus status = manifestReader->readEntryAt(offset, record);
  return status == RecordStatus::Record || refuse(result, SyncPlanStatus::ManifestError, status);
}

bool SyncPlanner::readTreeAt(uint32_t offset, SyncRecord& record, SyncPlanResult& result) {
  const RecordStatus status = treeReader->readEntryAt(offset, record);
  return status == RecordStatus::Record || refuse(result, SyncPlanStatus::TreeError, status);
}

bool SyncPlanner::checkLocal(SyncLocalExistsFn localExists, void* existsCtx, SyncPlanResult& result) {
  for (size_t i = 0; i < slotCount; ++i) {
    Slot& slot = slots[i];
    if (slotState(slot.offsetAndState) != REMOTE_NEW) continue;
    SyncRecord entry;
    if (!readTreeAt(slotOffset(slot.offsetAndState), entry, result)) return false;
    if (localExists(existsCtx, entry.path, entry.pathLen)) slot.offsetAndState |= FLAG_TARGET_EXISTS;
  }
  return true;
}

void SyncPlanner::countOps(SyncPlanResult& result) {
  for (size_t i = 0; i < slotCount; ++i) {
    switch (emitGroup(slots[i].offsetAndState)) {
      case GROUP_ADOPT:
        ++result.adoptOrDownloads;
        break;
      case GROUP_DOWNLOAD:
        ++result.downloads;
        break;
      case GROUP_REMOVED:
        ++result.deletes;
        if (slots[i].offsetAndState & FLAG_NESTED) ++result.prunes;
        break;
      case GROUP_NONE:
        break;
    }
  }
}

bool SyncPlanner::emit(const SyncOpSink& sink) {
  std::sort(slots, slots + slotCount, [](const Slot& a, const Slot& b) {
    const EmitGroup groupA = emitGroup(a.offsetAndState);
    const EmitGroup groupB = emitGroup(b.offsetAndState);
    if (groupA != groupB) return groupA < groupB;
    return slotOffset(a.offsetAndState) < slotOffset(b.offsetAndState);
  });
  size_t i = 0;
  for (; i < slotCount && emitGroup(slots[i].offsetAndState) < GROUP_REMOVED; ++i) {
    const uint32_t offsetAndState = slots[i].offsetAndState;
    const SyncOpKind kind =
        emitGroup(offsetAndState) == GROUP_ADOPT ? SyncOpKind::AdoptOrDownload : SyncOpKind::Download;
    const uint32_t replaced = slotState(offsetAndState) == REMOTE_PATH_MATCHED ? slots[i].shaPrefix : NO_OFFSET;
    if (!sink.onOp(sink.ctx, SyncOp{kind, SyncSkipReason::None, false, false, replaced, slotOffset(offsetAndState)})) {
      return false;
    }
  }
  const size_t removedBegin = i;
  for (; i < slotCount && emitGroup(slots[i].offsetAndState) == GROUP_REMOVED; ++i) {
    const SyncOp op{SyncOpKind::Delete, SyncSkipReason::None, false, false, slotOffset(slots[i].offsetAndState),
                    NO_OFFSET};
    if (!sink.onOp(sink.ctx, op)) return false;
  }
  for (size_t j = removedBegin; j < i; ++j) {
    if (!(slots[j].offsetAndState & FLAG_NESTED)) continue;
    const SyncOp op{SyncOpKind::Prune, SyncSkipReason::None, false, false, slotOffset(slots[j].offsetAndState),
                    NO_OFFSET};
    if (!sink.onOp(sink.ctx, op)) return false;
  }
  return true;
}
