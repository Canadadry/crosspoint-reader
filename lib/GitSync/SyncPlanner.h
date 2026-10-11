#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "RecordFormat.h"
#include "RecordReader.h"
#include "RecordStream.h"

// Largest library the planner accepts, counted in syncable entries per file (manifest and tree each).
// The worst case, every file moved, takes one 12-byte slot per side: 24 B per file, 48,000 B at 2,000.
// The 51,200 B budget asserted below fits about 2,091; raising this means re-checking the 64 KB sync heap.
constexpr size_t MAX_SYNC_FILES = 2000;

// Emitted in this order: Move, AdoptOrDownload, Download, Delete, Prune, Skip.
enum class SyncOpKind : uint8_t { Move, AdoptOrDownload, Download, Delete, Prune, Skip };

enum class SyncSkipReason : uint8_t { None, CaseCollision, UnsafeName };

// Offsets name entry frames: manifestOffset in the manifest, treeOffset in the tree, NO_OFFSET when unused.
// Move: manifestOffset is the source, treeOffset the destination, sourceExists and targetExists as planned.
// Download: manifestOffset is the replaced entry of a modify, NO_OFFSET for a new file.
// Delete and Prune: manifestOffset only. Skip: treeOffset and reason.
struct SyncOp {
  SyncOpKind kind;
  SyncSkipReason reason;
  bool sourceExists;
  bool targetExists;
  uint32_t manifestOffset;
  uint32_t treeOffset;
};

enum class SyncPlanStatus : uint8_t { Ok, ManifestError, TreeError, ManifestNotCompacted, CapExceeded, Aborted };

struct SyncPlanResult {
  SyncPlanStatus status = SyncPlanStatus::Ok;
  // The failing reader's status for ManifestError and TreeError.
  RecordStatus readerStatus = RecordStatus::End;
  uint32_t moves = 0;
  uint32_t adoptOrDownloads = 0;
  uint32_t downloads = 0;
  uint32_t deletes = 0;
  uint32_t prunes = 0;
  uint32_t caseCollisionSkips = 0;
  uint32_t unsafeNameSkips = 0;
  // Entries outside the sync's scope, in either file.
  uint32_t hiddenIgnored = 0;
  uint32_t malformedIgnored = 0;
  // Syncable entries read, compared with MAX_SYNC_FILES.
  uint32_t manifestEntries = 0;
  uint32_t treeEntries = 0;
};

// path is repository-relative and '\0'-terminated. Must answer like FAT: case-insensitively.
using SyncLocalExistsFn = bool (*)(void* ctx, const char* path, size_t pathLen);
// onOp returning false stops the planner with Aborted. onTotal, when not null, is called once with the
// number of operations before the first one, 0 included, and never when the plan fails.
struct SyncOpSink {
  void* ctx;
  bool (*onOp)(void* ctx, const SyncOp& op);
  void (*onTotal)(void* ctx, uint32_t totalOps);
};

// Plans a sync from a compacted manifest and a tree file, both at their first byte.
// Reads only those two files and calls localExists; every read happens before the first operation is emitted.
// About 49 KB: allocate it with makeUniqueNoThrow for one plan, never on the stack.
class SyncPlanner {
 public:
  SyncPlanResult plan(RecordSource& manifest, RecordSource& tree, SyncLocalExistsFn localExists, void* existsCtx,
                      const SyncOpSink& sink);

 private:
  // shaPrefix: first 4 SHA bytes, big-endian; a linked remote slot holds the manifest offset it replaces instead.
  // offsetAndState: frame offset in the low 24 bits, state and flags in the high 8.
  struct Slot {
    uint32_t shaPrefix;
    uint32_t pathKey;
    uint32_t offsetAndState;
  };
  static_assert(sizeof(Slot) == 12, "one slot per entry and side is the planner's whole RAM budget");

  static constexpr size_t SLOT_CAPACITY = 2 * MAX_SYNC_FILES;

  bool loadManifest(RecordSource& source, SyncPlanResult& result);
  bool streamTree(RecordSource& source, SyncPlanResult& result);
  bool findUnchanged(const Slot& remote, const SyncRecord& entry, bool& unchanged, SyncPlanResult& result);
  bool matchPaths(SyncPlanResult& result);
  bool matchRemote(Slot& remote, size_t runBegin, size_t runEnd, SyncPlanResult& result);
  bool checkLocal(SyncLocalExistsFn localExists, void* existsCtx, SyncPlanResult& result);
  void countOps(SyncPlanResult& result);
  bool emit(const SyncOpSink& sink);
  bool readManifestAt(uint32_t offset, SyncRecord& record, SyncPlanResult& result);
  bool readTreeAt(uint32_t offset, SyncRecord& record, SyncPlanResult& result);

  Slot slots[SLOT_CAPACITY];
  size_t manifestSlots = 0;
  size_t slotCount = 0;
  bool overCap = false;
  std::optional<RecordReader> manifestReader;
  std::optional<RecordReader> treeReader;
  // One path read back, with its terminator, and its SHA.
  char scratchPath[MAX_SYNC_PATH_BYTES + 1];
  uint8_t scratchSha[GitBlobHasher::SHA_SIZE];
};

static_assert(sizeof(SyncPlanner) <= 51200, "SyncPlanner exceeds its 50 KiB share of the 64 KB sync heap");
