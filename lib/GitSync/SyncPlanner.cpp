#include "SyncPlanner.h"

#include <algorithm>
#include <cstring>

#include "SyncPath.h"
#include "SyncPathFold.h"

namespace {

constexpr uint32_t OFFSET_MASK = 0x00FFFFFFu;
constexpr uint32_t STATE_SHIFT = 28;
constexpr uint32_t STATE_MASK = 0xFu << STATE_SHIFT;
// Set on a nested manifest entry; cleared on a move source that stays in its folder.
constexpr uint32_t FLAG_PRUNE = 1u << 24;
constexpr uint32_t FLAG_TARGET_EXISTS = 1u << 25;
constexpr uint32_t FLAG_SOURCE_EXISTS = 1u << 26;

enum SlotState : uint32_t {
  MANIFEST_MISSING,
  MANIFEST_UNCHANGED,
  MANIFEST_PATH_MATCHED,
  MANIFEST_MOVE_SOURCE,
  REMOTE_NEW,
  // A case-only rename: same SHA at a path equal after folding, nothing to do.
  REMOTE_UNCHANGED,
  // A modify: shaPrefix holds the replaced manifest offset.
  REMOTE_PATH_MATCHED,
  // shaPrefix holds the source manifest offset.
  REMOTE_MOVE_TARGET,
  REMOTE_CASE_COLLISION,
  REMOTE_UNSAFE_NAME,
};

uint32_t slotOffset(uint32_t offsetAndState) { return offsetAndState & OFFSET_MASK; }
uint32_t slotState(uint32_t offsetAndState) { return offsetAndState >> STATE_SHIFT; }
uint32_t withState(uint32_t offsetAndState, uint32_t state) {
  return (offsetAndState & ~STATE_MASK) | (state << STATE_SHIFT);
}
bool isManifestSlot(uint32_t offsetAndState) { return slotState(offsetAndState) < REMOTE_NEW; }

// Emission order; slots in GROUP_NONE emit nothing.
// GROUP_REMOVED emits its deletes, then the prunes of its deletes and move sources.
enum EmitGroup : uint32_t { GROUP_MOVE, GROUP_ADOPT, GROUP_DOWNLOAD, GROUP_REMOVED, GROUP_SKIP, GROUP_NONE };

EmitGroup emitGroup(uint32_t offsetAndState) {
  switch (slotState(offsetAndState)) {
    case REMOTE_MOVE_TARGET:
      return GROUP_MOVE;
    case MANIFEST_MISSING:
    case MANIFEST_MOVE_SOURCE:
      return GROUP_REMOVED;
    case REMOTE_NEW:
      return (offsetAndState & FLAG_TARGET_EXISTS) ? GROUP_ADOPT : GROUP_DOWNLOAD;
    case REMOTE_PATH_MATCHED:
      return GROUP_DOWNLOAD;
    case REMOTE_CASE_COLLISION:
    case REMOTE_UNSAFE_NAME:
      return GROUP_SKIP;
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
  return record.offset | (nested ? FLAG_PRUNE : 0) | (static_cast<uint32_t>(state) << STATE_SHIFT);
}

// Moves follow their source's offset, every other operation its own.
uint32_t emitOffset(const auto& slot) {
  return slotState(slot.offsetAndState) == REMOTE_MOVE_TARGET ? slot.shaPrefix : slotOffset(slot.offsetAndState);
}

size_t parentLength(const SyncRecord& record) {
  size_t len = record.pathLen;
  while (len > 0 && record.path[len - 1] != '/') --len;
  return len;
}

// Byte-wise, a shorter prefix first.
int comparePaths(const char* a, size_t aLen, const char* b, size_t bLen) {
  const int order = memcmp(a, b, std::min(aLen, bLen));
  if (order != 0) return order;
  return aLen < bLen ? -1 : (aLen > bLen ? 1 : 0);
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
  if (!matchPaths(result) || !checkLocal(localExists, existsCtx, result) ||
      !pairMoves(localExists, existsCtx, result)) {
    return result;
  }
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
    const SlotState state = isFatSafeName(record.path, record.pathLen) ? REMOTE_NEW : REMOTE_UNSAFE_NAME;
    const Slot remote{shaPrefixOf(record.sha), syncPathKey(record.path, record.pathLen),
                      packOffsetAndState(record, state)};
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

// Splits each run of equal path keys into classes of paths equal after folding, then resolves each class.
bool SyncPlanner::matchPaths(SyncPlanResult& result) {
  std::sort(slots, slots + slotCount, [](const Slot& a, const Slot& b) {
    return a.pathKey != b.pathKey ? a.pathKey < b.pathKey : a.offsetAndState < b.offsetAndState;
  });
  size_t runEnd = 0;
  for (size_t runBegin = 0; runBegin < slotCount; runBegin = runEnd) {
    runEnd = runBegin + 1;
    while (runEnd < slotCount && slots[runEnd].pathKey == slots[runBegin].pathKey) ++runEnd;
    // Each class is gathered at the front of what is left of the run.
    size_t classBegin = runBegin;
    while (runEnd - classBegin > 1 && hasNewRemote(classBegin, runEnd)) {
      SyncRecord record;
      if (!readSlot(slots[classBegin], record, result)) return false;
      const size_t leaderLen = record.pathLen;
      memcpy(scratchPath, record.path, leaderLen + 1);
      size_t classEnd = classBegin + 1;
      for (size_t k = classBegin + 1; k < runEnd; ++k) {
        if (!readSlot(slots[k], record, result)) return false;
        if (syncPathsEqualFolded(scratchPath, leaderLen, record.path, record.pathLen)) {
          std::swap(slots[classEnd++], slots[k]);
        }
      }
      if (!resolveClass(classBegin, classEnd, result)) return false;
      classBegin = classEnd;
    }
  }
  return true;
}

bool SyncPlanner::hasNewRemote(size_t begin, size_t end) const {
  for (size_t i = begin; i < end; ++i) {
    if (slotState(slots[i].offsetAndState) == REMOTE_NEW) return true;
  }
  return false;
}

// Slots of one FAT path: one remote wins, the other remotes are case collisions, and the winner
// path-matches the missing manifest entry if there is one.
bool SyncPlanner::resolveClass(size_t begin, size_t end, SyncPlanResult& result) {
  Slot* stored = nullptr;
  bool pathKept = false;
  for (size_t i = begin; i < end; ++i) {
    const uint32_t offsetAndState = slots[i].offsetAndState;
    const uint32_t state = slotState(offsetAndState);
    // An unchanged entry is a remote path, byte-identical to the manifest's, that already won.
    if (state == MANIFEST_UNCHANGED) pathKept = true;
    if (state == MANIFEST_MISSING && (!stored || slotOffset(offsetAndState) < slotOffset(stored->offsetAndState))) {
      stored = &slots[i];
    }
  }
  Slot* winner = nullptr;
  bool sameSha = false;
  if (!pathKept && !findWinner(begin, end, stored, winner, sameSha, result)) return false;
  for (size_t i = begin; i < end; ++i) {
    if (slotState(slots[i].offsetAndState) == REMOTE_NEW && &slots[i] != winner) {
      slots[i].offsetAndState = withState(slots[i].offsetAndState, REMOTE_CASE_COLLISION);
    }
  }
  if (!winner || !stored) return true;
  stored->offsetAndState = withState(stored->offsetAndState, MANIFEST_PATH_MATCHED);
  if (sameSha) {
    winner->offsetAndState = withState(winner->offsetAndState, REMOTE_UNCHANGED);
  } else {
    winner->offsetAndState = withState(winner->offsetAndState, REMOTE_PATH_MATCHED);
    winner->shaPrefix = slotOffset(stored->offsetAndState);
  }
  return true;
}

// The remote byte-identical to the stored path, otherwise the byte-wise smallest remote path.
// sameSha compares the winner with stored, when there is one.
bool SyncPlanner::findWinner(size_t begin, size_t end, const Slot* stored, Slot*& winner, bool& sameSha,
                             SyncPlanResult& result) {
  // The manifest reader's record stays valid while only the tree is read.
  SyncRecord kept;
  SyncRecord record;
  if (stored) {
    if (!readManifestAt(slotOffset(stored->offsetAndState), kept, result)) return false;
    for (size_t i = begin; i < end; ++i) {
      if (slotState(slots[i].offsetAndState) != REMOTE_NEW) continue;
      if (!readTreeAt(slotOffset(slots[i].offsetAndState), record, result)) return false;
      if (comparePaths(record.path, record.pathLen, kept.path, kept.pathLen) == 0) {
        winner = &slots[i];
        sameSha = memcmp(record.sha, kept.sha, GitBlobHasher::SHA_SIZE) == 0;
        return true;
      }
    }
  }
  size_t winnerLen = 0;
  for (size_t i = begin; i < end; ++i) {
    if (slotState(slots[i].offsetAndState) != REMOTE_NEW) continue;
    if (!readTreeAt(slotOffset(slots[i].offsetAndState), record, result)) return false;
    if (winner && comparePaths(record.path, record.pathLen, scratchPath, winnerLen) >= 0) continue;
    winner = &slots[i];
    winnerLen = record.pathLen;
    memcpy(scratchPath, record.path, winnerLen + 1);
    memcpy(scratchSha, record.sha, GitBlobHasher::SHA_SIZE);
  }
  sameSha = stored && memcmp(scratchSha, kept.sha, GitBlobHasher::SHA_SIZE) == 0;
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

bool SyncPlanner::readSlot(const Slot& slot, SyncRecord& record, SyncPlanResult& result) {
  const uint32_t offset = slotOffset(slot.offsetAndState);
  return isManifestSlot(slot.offsetAndState) ? readManifestAt(offset, record, result)
                                             : readTreeAt(offset, record, result);
}

// Pairs missing and new slots of the same full SHA, each side in byte-wise path order, by rank.
bool SyncPlanner::pairMoves(SyncLocalExistsFn localExists, void* existsCtx, SyncPlanResult& result) {
  Slot* const pendingEnd = std::partition(slots, slots + slotCount, [](const Slot& slot) {
    const uint32_t state = slotState(slot.offsetAndState);
    return state == MANIFEST_MISSING || state == REMOTE_NEW;
  });
  const size_t pending = pendingEnd - slots;
  std::sort(slots, pendingEnd, [](const Slot& a, const Slot& b) { return a.shaPrefix < b.shaPrefix; });
  size_t runEnd = 0;
  for (size_t runBegin = 0; runBegin < pending; runBegin = runEnd) {
    const uint32_t prefix = slots[runBegin].shaPrefix;
    runEnd = runBegin + 1;
    while (runEnd < pending && slots[runEnd].shaPrefix == prefix) ++runEnd;
    // Groups of one true SHA are gathered at the front of what is left of the run.
    size_t groupBegin = runBegin;
    while (hasBothSides(groupBegin, runEnd)) {
      SyncRecord record;
      if (!readSlot(slots[groupBegin], record, result)) return false;
      memcpy(scratchSha, record.sha, GitBlobHasher::SHA_SIZE);
      size_t groupEnd = groupBegin + 1;
      for (size_t k = groupBegin + 1; k < runEnd; ++k) {
        if (!readSlot(slots[k], record, result)) return false;
        if (memcmp(record.sha, scratchSha, GitBlobHasher::SHA_SIZE) == 0) std::swap(slots[groupEnd++], slots[k]);
      }
      if (!pairGroup(groupBegin, groupEnd, localExists, existsCtx, result)) return false;
      groupBegin = groupEnd;
    }
  }
  return true;
}

bool SyncPlanner::hasBothSides(size_t begin, size_t end) const {
  bool manifestSide = false;
  bool remoteSide = false;
  for (size_t i = begin; i < end; ++i) {
    (isManifestSlot(slots[i].offsetAndState) ? manifestSide : remoteSide) = true;
  }
  return manifestSide && remoteSide;
}

bool SyncPlanner::pairGroup(size_t begin, size_t end, SyncLocalExistsFn localExists, void* existsCtx,
                            SyncPlanResult& result) {
  Slot* const first = slots + begin;
  Slot* const last = slots + end;
  Slot* const remoteBegin =
      std::partition(first, last, [](const Slot& slot) { return isManifestSlot(slot.offsetAndState); });
  if (!sortByPath(first, remoteBegin, result) || !sortByPath(remoteBegin, last, result)) return false;
  const size_t pairs = std::min(remoteBegin - first, last - remoteBegin);
  for (size_t i = 0; i < pairs; ++i) {
    if (!pairMove(first[i], remoteBegin[i], localExists, existsCtx, result)) return false;
  }
  return true;
}

// A heap sort: its comparator reads paths and may fail, and heap operations stay in bounds whatever it answers.
bool SyncPlanner::sortByPath(Slot* first, Slot* last, SyncPlanResult& result) {
  bool failed = false;
  const auto less = [&](const Slot& a, const Slot& b) {
    if (!failed) {
      SyncRecord record;
      failed = !readSlot(a, record, result);
      if (!failed) {
        const size_t aLen = record.pathLen;
        memcpy(scratchPath, record.path, aLen + 1);
        failed = !readSlot(b, record, result);
        const int order = failed ? 0 : comparePaths(scratchPath, aLen, record.path, record.pathLen);
        if (order != 0) return order < 0;
      }
    }
    return slotOffset(a.offsetAndState) < slotOffset(b.offsetAndState);
  };
  std::make_heap(first, last, less);
  std::sort_heap(first, last, less);
  return !failed;
}

// Applies the existence table: a move only when exactly one of source and target is on the card.
bool SyncPlanner::pairMove(Slot& source, Slot& target, SyncLocalExistsFn localExists, void* existsCtx,
                           SyncPlanResult& result) {
  SyncRecord from;
  SyncRecord to;
  if (!readManifestAt(slotOffset(source.offsetAndState), from, result) ||
      !readTreeAt(slotOffset(target.offsetAndState), to, result)) {
    return false;
  }
  const bool sourceExists = localExists(existsCtx, from.path, from.pathLen);
  const bool targetExists = (target.offsetAndState & FLAG_TARGET_EXISTS) != 0;
  if (sourceExists == targetExists) return true;
  source.offsetAndState = withState(source.offsetAndState, MANIFEST_MOVE_SOURCE);
  if (syncPathsEqualFolded(from.path, parentLength(from), to.path, parentLength(to))) {
    source.offsetAndState &= ~FLAG_PRUNE;
  }
  target.offsetAndState =
      withState(target.offsetAndState, REMOTE_MOVE_TARGET) | (sourceExists ? FLAG_SOURCE_EXISTS : 0);
  target.shaPrefix = from.offset;
  return true;
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
      case GROUP_MOVE:
        ++result.moves;
        break;
      case GROUP_ADOPT:
        ++result.adoptOrDownloads;
        break;
      case GROUP_DOWNLOAD:
        ++result.downloads;
        break;
      case GROUP_SKIP:
        ++(slotState(slots[i].offsetAndState) == REMOTE_UNSAFE_NAME ? result.unsafeNameSkips
                                                                    : result.caseCollisionSkips);
        break;
      case GROUP_REMOVED:
        if (slotState(slots[i].offsetAndState) == MANIFEST_MISSING) ++result.deletes;
        if (slots[i].offsetAndState & FLAG_PRUNE) ++result.prunes;
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
    return emitOffset(a) < emitOffset(b);
  });
  size_t i = 0;
  for (; i < slotCount && emitGroup(slots[i].offsetAndState) == GROUP_MOVE; ++i) {
    const uint32_t offsetAndState = slots[i].offsetAndState;
    const SyncOp op{SyncOpKind::Move,
                    SyncSkipReason::None,
                    (offsetAndState & FLAG_SOURCE_EXISTS) != 0,
                    (offsetAndState & FLAG_TARGET_EXISTS) != 0,
                    slots[i].shaPrefix,
                    slotOffset(offsetAndState)};
    if (!sink.onOp(sink.ctx, op)) return false;
  }
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
    if (slotState(slots[i].offsetAndState) != MANIFEST_MISSING) continue;
    const SyncOp op{SyncOpKind::Delete, SyncSkipReason::None, false, false, slotOffset(slots[i].offsetAndState),
                    NO_OFFSET};
    if (!sink.onOp(sink.ctx, op)) return false;
  }
  for (size_t j = removedBegin; j < i; ++j) {
    if (!(slots[j].offsetAndState & FLAG_PRUNE)) continue;
    const SyncOp op{SyncOpKind::Prune, SyncSkipReason::None, false, false, slotOffset(slots[j].offsetAndState),
                    NO_OFFSET};
    if (!sink.onOp(sink.ctx, op)) return false;
  }
  for (; i < slotCount && emitGroup(slots[i].offsetAndState) == GROUP_SKIP; ++i) {
    const uint32_t offsetAndState = slots[i].offsetAndState;
    const SyncSkipReason reason =
        slotState(offsetAndState) == REMOTE_UNSAFE_NAME ? SyncSkipReason::UnsafeName : SyncSkipReason::CaseCollision;
    const SyncOp op{SyncOpKind::Skip, reason, false, false, NO_OFFSET, slotOffset(offsetAndState)};
    if (!sink.onOp(sink.ctx, op)) return false;
  }
  return true;
}
