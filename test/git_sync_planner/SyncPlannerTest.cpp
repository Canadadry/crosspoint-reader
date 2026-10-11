#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "MemoryRecordStream.h"
#include "RecordWriter.h"
#include "SyncPathFold.h"
#include "SyncPlanner.h"

bool operator==(const SyncOp& a, const SyncOp& b) {
  return a.kind == b.kind && a.reason == b.reason && a.sourceExists == b.sourceExists &&
         a.targetExists == b.targetExists && a.manifestOffset == b.manifestOffset && a.treeOffset == b.treeOffset;
}

void PrintTo(const SyncOp& op, std::ostream* os) {
  static const char* const KIND_NAMES[] = {"Move", "AdoptOrDownload", "Download", "Delete", "Prune", "Skip"};
  *os << KIND_NAMES[static_cast<int>(op.kind)] << "{manifest=" << op.manifestOffset << " tree=" << op.treeOffset
      << " reason=" << static_cast<int>(op.reason) << " src=" << op.sourceExists << " dst=" << op.targetExists << "}";
}

namespace {

// The SHA of content id: id big-endian in the first 4 bytes, so ids map one-to-one onto prefixes.
// A non-zero tail changes the last byte only: another SHA with the same prefix.
struct Entry {
  std::string path;
  uint32_t id;
  uint8_t tail = 0;
};

void shaFor(uint32_t id, uint8_t sha[GitBlobHasher::SHA_SIZE], uint8_t tail = 0) {
  for (size_t i = 0; i < GitBlobHasher::SHA_SIZE; ++i) {
    sha[i] = static_cast<uint8_t>(i < 4 ? id >> (8 * (3 - i)) : (id * 31 + i * 7));
  }
  sha[GitBlobHasher::SHA_SIZE - 1] ^= tail;
}

struct RecordFile {
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> offsets;
};

RecordFile writeFile(RecordFileKind kind, const std::vector<Entry>& entries) {
  RecordFile file;
  file.bytes.resize(RECORD_HEADER_SIZE + (entries.size() + 1) * MAX_FRAME_SIZE);
  file.offsets.reserve(entries.size());
  MemoryRecordStream stream(file.bytes.data(), file.bytes.size());
  RecordWriter writer(stream);
  EXPECT_TRUE(writer.begin(kind));
  for (const Entry& entry : entries) {
    uint8_t sha[GitBlobHasher::SHA_SIZE];
    shaFor(entry.id, sha, entry.tail);
    uint32_t offset = NO_OFFSET;
    EXPECT_TRUE(writer.put(sha, 100 + entry.id, entry.path.data(), entry.path.size(), NO_OFFSET, &offset));
    file.offsets.push_back(offset);
  }
  EXPECT_TRUE(writer.seal());
  file.bytes.resize(stream.length());
  return file;
}

RecordFile manifestOf(const std::vector<Entry>& entries) { return writeFile(RecordFileKind::Manifest, entries); }
RecordFile treeOf(const std::vector<Entry>& entries) { return writeFile(RecordFileKind::Tree, entries); }

struct Card {
  std::vector<std::string> paths;
  int calls = 0;
};

bool cardExists(void* ctx, const char* path, size_t pathLen) {
  auto* card = static_cast<Card*>(ctx);
  ++card->calls;
  EXPECT_EQ(path[pathLen], '\0');
  return std::any_of(card->paths.begin(), card->paths.end(),
                     [&](const std::string& p) { return syncPathsEqualFolded(p.data(), p.size(), path, pathLen); });
}

struct Recorder {
  std::vector<SyncOp> ops;
  size_t stopAt = 0;  // Refuses this call (1-based); 0 never.
  std::vector<uint32_t> totals;
  size_t opsBeforeTotal = 0;
};

void recordTotal(void* ctx, uint32_t totalOps) {
  auto* recorder = static_cast<Recorder*>(ctx);
  recorder->totals.push_back(totalOps);
  recorder->opsBeforeTotal = recorder->ops.size();
}

bool recordOp(void* ctx, const SyncOp& op) {
  auto* recorder = static_cast<Recorder*>(ctx);
  recorder->ops.push_back(op);
  return recorder->ops.size() != recorder->stopAt;
}

struct PlanRun {
  SyncPlanResult result;
  std::vector<SyncOp> ops;
  std::vector<uint32_t> totals;
  size_t opsBeforeTotal = 0;
};

// Nothing outside the manifest may be deleted, moved or pruned.
void expectOnlyManifestRemoved(const RecordFile& manifest, const std::vector<SyncOp>& ops) {
  for (const SyncOp& op : ops) {
    if (op.kind == SyncOpKind::Delete || op.kind == SyncOpKind::Prune || op.kind == SyncOpKind::Move) {
      EXPECT_NE(std::find(manifest.offsets.begin(), manifest.offsets.end(), op.manifestOffset), manifest.offsets.end());
    }
  }
}

// A completed plan reports its total once, before any operation; a failed plan reports none.
void expectTotalMatches(const PlanRun& run, size_t stopAt) {
  if (run.result.status != SyncPlanStatus::Ok && run.result.status != SyncPlanStatus::Aborted) {
    EXPECT_EQ(run.totals, std::vector<uint32_t>{});
    return;
  }
  ASSERT_EQ(run.totals.size(), 1u);
  EXPECT_EQ(run.opsBeforeTotal, 0u);
  if (stopAt == 0) {
    EXPECT_EQ(run.totals[0], run.ops.size());
  } else {
    EXPECT_GE(run.totals[0], run.ops.size());
  }
}

PlanRun runPlan(RecordSource& manifestSource, RecordSource& treeSource, Card& card, size_t stopAt = 0) {
  auto planner = std::make_unique<SyncPlanner>();
  Recorder recorder;
  recorder.stopAt = stopAt;
  PlanRun run;
  const SyncOpSink sink{&recorder, recordOp, recordTotal};
  run.result = planner->plan(manifestSource, treeSource, cardExists, &card, sink);
  run.ops = recorder.ops;
  run.totals = recorder.totals;
  run.opsBeforeTotal = recorder.opsBeforeTotal;
  expectTotalMatches(run, stopAt);
  return run;
}

PlanRun runPlan(RecordFile& manifest, RecordFile& tree, std::vector<std::string> onCard = {}, size_t stopAt = 0) {
  MemoryRecordStream manifestSource(manifest.bytes.data(), manifest.bytes.size(), manifest.bytes.size());
  MemoryRecordStream treeSource(tree.bytes.data(), tree.bytes.size(), tree.bytes.size());
  Card card{std::move(onCard)};
  PlanRun run = runPlan(manifestSource, treeSource, card, stopAt);
  expectOnlyManifestRemoved(manifest, run.ops);
  return run;
}

SyncOp op(SyncOpKind kind, uint32_t manifestOffset, uint32_t treeOffset) {
  return SyncOp{kind, SyncSkipReason::None, false, false, manifestOffset, treeOffset};
}
SyncOp download(uint32_t treeOffset, uint32_t replaced = NO_OFFSET) {
  return op(SyncOpKind::Download, replaced, treeOffset);
}
SyncOp adopt(uint32_t treeOffset) { return op(SyncOpKind::AdoptOrDownload, NO_OFFSET, treeOffset); }
SyncOp remove(uint32_t manifestOffset) { return op(SyncOpKind::Delete, manifestOffset, NO_OFFSET); }
SyncOp prune(uint32_t manifestOffset) { return op(SyncOpKind::Prune, manifestOffset, NO_OFFSET); }

using Ops = std::vector<SyncOp>;

const std::vector<Entry> LIBRARY = {
    {"Hugo/Les Mis\xC3\xA9rables.epub", 1},
    {"Hugo/Notre-Dame.epub", 2},
    {"Verne/20000 lieues.epub", 3},
    {"readme.txt", 4},
};

TEST(SyncPlanner, NoChangePlansNothing) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf(LIBRARY);
  const PlanRun run = runPlan(manifest, tree, {"Hugo/Les Mis\xC3\xA9rables.epub", "readme.txt"});
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, Ops{});
  EXPECT_EQ(run.result.manifestEntries, 4u);
  EXPECT_EQ(run.result.treeEntries, 4u);
}

std::vector<Entry> with(std::vector<Entry> entries, const Entry& extra) {
  entries.push_back(extra);
  return entries;
}

TEST(SyncPlanner, AddedFileMissingFromCardIsDownloaded) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf(with(LIBRARY, {"Zola/Germinal.epub", 9}));
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, Ops{download(tree.offsets[4])});
  EXPECT_EQ(run.result.downloads, 1u);
}

TEST(SyncPlanner, AddedFileAlreadyOnCardIsAdoptedOrDownloaded) {
  RecordFile manifest = manifestOf({});
  RecordFile tree = treeOf(LIBRARY);
  const PlanRun run = runPlan(manifest, tree, {"HUGO/notre-dame.EPUB", "readme.txt"});
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, (Ops{adopt(tree.offsets[1]), adopt(tree.offsets[3]), download(tree.offsets[0]),
                          download(tree.offsets[2])}));
  EXPECT_EQ(run.result.adoptOrDownloads, 2u);
  EXPECT_EQ(run.result.downloads, 2u);
}

TEST(SyncPlanner, DeletedNestedFileIsDeletedThenPruned) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf({LIBRARY[0], LIBRARY[2], LIBRARY[3]});
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, (Ops{remove(manifest.offsets[1]), prune(manifest.offsets[1])}));
  EXPECT_EQ(run.result.deletes, 1u);
  EXPECT_EQ(run.result.prunes, 1u);
}

TEST(SyncPlanner, DeletedRootFileIsDeletedWithoutPrune) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf({LIBRARY[0], LIBRARY[1], LIBRARY[2]});
  const PlanRun run = runPlan(manifest, tree, {"readme.txt"});
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, Ops{remove(manifest.offsets[3])});
  EXPECT_EQ(run.result.prunes, 0u);
}

TEST(SyncPlanner, ModifiedFileIsDownloadedReplacingItsEntry) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf({LIBRARY[0], {LIBRARY[1].path, 20}, LIBRARY[2], LIBRARY[3]});
  const PlanRun run = runPlan(manifest, tree, {LIBRARY[1].path});
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, Ops{download(tree.offsets[1], manifest.offsets[1])});
  EXPECT_EQ(run.result.downloads, 1u);
  EXPECT_EQ(run.result.deletes, 0u);
}

TEST(SyncPlanner, CaseOnlyRenameWithSameContentPlansNothing) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf({LIBRARY[0], {"hugo/NOTRE-DAME.epub", 2}, LIBRARY[2], {"README.TXT", 4}});
  const PlanRun run = runPlan(manifest, tree, {LIBRARY[1].path, LIBRARY[3].path});
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, Ops{});
}

TEST(SyncPlanner, CaseOnlyRenameWithNewContentReplacesTheOldEntry) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf({{"HUGO/Les MIS\xC3\x89RABLES.epub", 21}, LIBRARY[1], LIBRARY[2], LIBRARY[3]});
  const PlanRun run = runPlan(manifest, tree, {LIBRARY[0].path});
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, Ops{download(tree.offsets[0], manifest.offsets[0])});
}

TEST(SyncPlanner, HiddenRemoteEntriesAreIgnoredAndCounted) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf(
      with(with(with(with(LIBRARY, {".crosspoint/epub_1/book.bin", 30}), {".github/ci.yml", 31}), {"a/.git/b", 32}),
           {"a//b", 33}));
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, Ops{});
  EXPECT_EQ(run.result.hiddenIgnored, 3u);
  EXPECT_EQ(run.result.malformedIgnored, 1u);
  EXPECT_EQ(run.result.treeEntries, 4u);
}

TEST(SyncPlanner, UnsyncableManifestEntriesAreNeverRemoved) {
  RecordFile manifest = manifestOf(with(with(LIBRARY, {".crosspoint/state.json", 40}), {" lead/x.epub", 41}));
  RecordFile tree = treeOf(LIBRARY);
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, Ops{});
  EXPECT_EQ(run.result.hiddenIgnored, 1u);
  EXPECT_EQ(run.result.malformedIgnored, 1u);
  EXPECT_EQ(run.result.manifestEntries, 4u);
}

std::vector<Entry> numbered(size_t count, const char* folder, uint32_t firstId) {
  std::vector<Entry> entries;
  entries.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    entries.push_back(
        {std::string(folder) + "/book" + std::to_string(i) + ".epub", firstId + static_cast<uint32_t>(i)});
  }
  return entries;
}

TEST(SyncPlanner, TreeAtTheCapIsPlanned) {
  RecordFile manifest = manifestOf({});
  RecordFile tree = treeOf(with(numbered(MAX_SYNC_FILES, "lib", 1000), {".crosspoint/x", 1}));
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops.size(), MAX_SYNC_FILES);
  EXPECT_EQ(run.result.treeEntries, MAX_SYNC_FILES);
}

TEST(SyncPlanner, TreeOverTheCapIsRefusedWithBothCounts) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf(numbered(MAX_SYNC_FILES + 1, "lib", 1000));
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::CapExceeded);
  EXPECT_EQ(run.ops, Ops{});
  EXPECT_EQ(run.result.treeEntries, MAX_SYNC_FILES + 1);
  EXPECT_EQ(run.result.manifestEntries, LIBRARY.size());
}

TEST(SyncPlanner, ManifestAtTheCapIsPlanned) {
  RecordFile manifest = manifestOf(with(numbered(MAX_SYNC_FILES, "lib", 1000), {".github/x", 1}));
  RecordFile tree = treeOf({});
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.result.deletes, MAX_SYNC_FILES);
  EXPECT_EQ(run.result.prunes, MAX_SYNC_FILES);
}

TEST(SyncPlanner, ManifestOverTheCapIsRefusedWithBothCounts) {
  RecordFile manifest = manifestOf(numbered(MAX_SYNC_FILES + 1, "lib", 1000));
  RecordFile tree = treeOf(numbered(MAX_SYNC_FILES + 5, "lib", 1000));
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::CapExceeded);
  EXPECT_EQ(run.ops, Ops{});
  EXPECT_EQ(run.result.manifestEntries, MAX_SYNC_FILES + 1);
  EXPECT_EQ(run.result.treeEntries, MAX_SYNC_FILES + 5);
}

enum class Damage { Truncated, Corrupt, WrongVersion };

void damage(RecordFile& file, Damage kind) {
  switch (kind) {
    case Damage::Truncated:
      file.bytes.resize(file.bytes.size() - 3);
      break;
    case Damage::Corrupt:
      file.bytes[file.offsets.back() + FRAME_HEADER_SIZE + 2] ^= 0x01;
      break;
    case Damage::WrongVersion:
      file.bytes[4] = RECORD_FILE_VERSION + 1;
      break;
  }
}

RecordStatus statusOf(Damage kind) {
  switch (kind) {
    case Damage::Truncated:
      return RecordStatus::Truncated;
    case Damage::Corrupt:
      return RecordStatus::Corrupt;
    case Damage::WrongVersion:
      break;
  }
  return RecordStatus::WrongVersion;
}

TEST(SyncPlanner, DamagedManifestStopsThePlanWithItsStatus) {
  for (const Damage kind : {Damage::Truncated, Damage::Corrupt, Damage::WrongVersion}) {
    RecordFile manifest = manifestOf(LIBRARY);
    damage(manifest, kind);
    RecordFile tree = treeOf(with(LIBRARY, {"new.epub", 50}));
    const PlanRun run = runPlan(manifest, tree);
    EXPECT_EQ(run.result.status, SyncPlanStatus::ManifestError);
    EXPECT_EQ(run.result.readerStatus, statusOf(kind));
    EXPECT_EQ(run.ops, Ops{});
  }
}

TEST(SyncPlanner, DamagedTreeStopsThePlanWithItsStatus) {
  for (const Damage kind : {Damage::Truncated, Damage::Corrupt, Damage::WrongVersion}) {
    RecordFile manifest = manifestOf(with(LIBRARY, {"gone.epub", 51}));
    RecordFile tree = treeOf(LIBRARY);
    damage(tree, kind);
    const PlanRun run = runPlan(manifest, tree);
    EXPECT_EQ(run.result.status, SyncPlanStatus::TreeError);
    EXPECT_EQ(run.result.readerStatus, statusOf(kind));
    EXPECT_EQ(run.ops, Ops{});
  }
}

TEST(SyncPlanner, FilesOfTheWrongKindAreCorrupt) {
  RecordFile tree = treeOf(LIBRARY);
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile treeAsManifest = tree;
  PlanRun run = runPlan(treeAsManifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::ManifestError);
  EXPECT_EQ(run.result.readerStatus, RecordStatus::Corrupt);
  EXPECT_EQ(run.ops, Ops{});

  RecordFile manifestAsTree = manifest;
  run = runPlan(manifest, manifestAsTree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::TreeError);
  EXPECT_EQ(run.result.readerStatus, RecordStatus::Corrupt);
}

// A manifest with a sealed base of LIBRARY, then one journal frame: an entry or a drop.
RecordFile journaledManifest(bool drop) {
  RecordFile file;
  file.bytes.resize(4096);
  MemoryRecordStream stream(file.bytes.data(), file.bytes.size());
  RecordWriter writer(stream);
  EXPECT_TRUE(writer.begin(RecordFileKind::Manifest));
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  for (const Entry& entry : LIBRARY) {
    shaFor(entry.id, sha);
    uint32_t offset = NO_OFFSET;
    EXPECT_TRUE(writer.put(sha, 1, entry.path.data(), entry.path.size(), NO_OFFSET, &offset));
    file.offsets.push_back(offset);
  }
  EXPECT_TRUE(writer.seal());
  if (drop) {
    EXPECT_TRUE(writer.drop(file.offsets[0]));
  } else {
    shaFor(60, sha);
    EXPECT_TRUE(writer.put(sha, 1, "j.epub", 6, file.offsets[1], nullptr));
  }
  file.bytes.resize(stream.length());
  return file;
}

TEST(SyncPlanner, ManifestWithAJournalIsRefusedAsNotCompacted) {
  for (const bool drop : {false, true}) {
    RecordFile manifest = journaledManifest(drop);
    RecordFile tree = treeOf(with(LIBRARY, {"new.epub", 50}));
    const PlanRun run = runPlan(manifest, tree);
    EXPECT_EQ(run.result.status, SyncPlanStatus::ManifestNotCompacted);
    EXPECT_EQ(run.ops, Ops{});
  }
}

// Fails its failAt-th call (read or seek, 1-based) once; counts seeks.
class FaultySource : public RecordSource {
 public:
  FaultySource(RecordFile& file, size_t failAt)
      : stream(file.bytes.data(), file.bytes.size(), file.bytes.size()), failAt(failAt) {}

  int read(uint8_t* buf, size_t len) override { return ++calls == failAt ? -1 : stream.read(buf, len); }

  bool seek(uint32_t offset) override {
    ++seeks;
    return ++calls == failAt ? false : stream.seek(offset);
  }

  MemoryRecordStream stream;
  size_t failAt;
  size_t calls = 0;
  size_t seeks = 0;
};

// Exercises every pass: unchanged, modify, case-only rename, add, adopt, delete.
struct MixedScenario {
  RecordFile manifest = manifestOf(with(LIBRARY, {"Old/gone.epub", 70}));
  RecordFile tree = treeOf({LIBRARY[0],
                            {LIBRARY[1].path, 71},
                            {"verne/20000 LIEUES.epub", 3},
                            LIBRARY[3],
                            {"New/added.epub", 72},
                            {"New/adopted.epub", 73}});
  std::vector<std::string> card = {"new/ADOPTED.epub"};
};

TEST(SyncPlanner, SourceFailingAtAnyCallStopsThePlanBeforeAnyOperation) {
  for (const bool failManifest : {true, false}) {
    size_t failAt = 1;
    for (;; ++failAt) {
      MixedScenario scenario;
      FaultySource manifestSource(scenario.manifest, failManifest ? failAt : 0);
      FaultySource treeSource(scenario.tree, failManifest ? 0 : failAt);
      Card card{scenario.card};
      const PlanRun run = runPlan(manifestSource, treeSource, card);
      if (run.result.status == SyncPlanStatus::Ok) {
        EXPECT_EQ(run.ops.size(), 5u);
        break;
      }
      EXPECT_EQ(run.result.status, failManifest ? SyncPlanStatus::ManifestError : SyncPlanStatus::TreeError) << failAt;
      EXPECT_EQ(run.result.readerStatus, RecordStatus::IoError) << failAt;
      EXPECT_EQ(run.ops, Ops{}) << failAt;
    }
    EXPECT_GT(failAt, 10u);
  }
}

TEST(SyncPlanner, OperationsComeInTheDocumentedOrder) {
  RecordFile manifest =
      manifestOf({{"b/gone2.epub", 80}, LIBRARY[0], {"root-gone.epub", 81}, LIBRARY[1], {"a/gone1.epub", 82}});
  RecordFile tree = treeOf({{"z/new1.epub", 83},
                            {LIBRARY[1].path, 84},
                            {"y/adopt1.epub", 85},
                            {"new2.epub", 86},
                            {LIBRARY[0].path, 87},
                            {"x/adopt2.epub", 88}});
  const PlanRun run = runPlan(manifest, tree, {"y/adopt1.epub", "x/adopt2.epub"});
  EXPECT_EQ(run.result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(run.ops, (Ops{adopt(tree.offsets[2]), adopt(tree.offsets[5]), download(tree.offsets[0]),
                          download(tree.offsets[1], manifest.offsets[3]), download(tree.offsets[3]),
                          download(tree.offsets[4], manifest.offsets[1]), remove(manifest.offsets[0]),
                          remove(manifest.offsets[2]), remove(manifest.offsets[4]), prune(manifest.offsets[0]),
                          prune(manifest.offsets[4])}));
}

TEST(SyncPlanner, SinkRefusingItsKthCallAbortsAfterExactlyKCalls) {
  for (size_t stopAt = 1; stopAt <= 5; ++stopAt) {
    MixedScenario scenario;
    const PlanRun run = runPlan(scenario.manifest, scenario.tree, scenario.card, stopAt);
    EXPECT_EQ(run.result.status, SyncPlanStatus::Aborted);
    EXPECT_EQ(run.ops.size(), stopAt);
  }
}

size_t seeksFor(RecordFile& manifest, RecordFile& tree, std::vector<std::string> onCard, SyncPlanResult& result) {
  FaultySource manifestSource(manifest, 0);
  FaultySource treeSource(tree, 0);
  Card card{std::move(onCard)};
  result = runPlan(manifestSource, treeSource, card).result;
  return manifestSource.seeks + treeSource.seeks;
}

constexpr size_t SEEK_SLACK = 16;

TEST(SyncPlanner, NoChangeAtTheCapReadsBackLinearly) {
  RecordFile manifest = manifestOf(numbered(MAX_SYNC_FILES, "lib", 1000));
  RecordFile tree = treeOf(numbered(MAX_SYNC_FILES, "lib", 1000));
  SyncPlanResult result;
  EXPECT_LE(seeksFor(manifest, tree, {}, result), 3 * MAX_SYNC_FILES + SEEK_SLACK);
  EXPECT_EQ(result.status, SyncPlanStatus::Ok);
}

TEST(SyncPlanner, IdenticalFilesAtTheCapReadBackLinearly) {
  std::vector<Entry> same = numbered(MAX_SYNC_FILES, "copies", 0);
  for (Entry& entry : same) entry.id = 7;
  RecordFile manifest = manifestOf(same);
  RecordFile tree = treeOf(same);
  SyncPlanResult result;
  EXPECT_LE(seeksFor(manifest, tree, {}, result), 3 * MAX_SYNC_FILES + SEEK_SLACK);
  EXPECT_EQ(result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(result.downloads + result.deletes, 0u);
}

TEST(SyncPlanner, AddsModifiesAndDeletesAtTheCapReadBackLinearly) {
  const std::vector<Entry> old = numbered(MAX_SYNC_FILES, "lib", 1000);
  std::vector<Entry> current(old.begin(), old.begin() + 1500);
  for (size_t i = 500; i < 1000; ++i) current[i].id += 5000;
  for (size_t i = 0; i < 500; ++i) current.push_back({"added/" + std::to_string(i) + ".epub", 9000 + uint32_t(i)});
  RecordFile manifest = manifestOf(old);
  RecordFile tree = treeOf(current);
  SyncPlanResult result;
  EXPECT_LE(seeksFor(manifest, tree, {}, result), 3 * MAX_SYNC_FILES + SEEK_SLACK);
  EXPECT_EQ(result.status, SyncPlanStatus::Ok);
  EXPECT_EQ(result.downloads, 1000u);
  EXPECT_EQ(result.deletes, 500u);
}

TEST(SyncPlanner, SamePrefixAtTheSamePathIsAModifyNotUnchanged) {
  RecordFile manifest = manifestOf({{"a/b.epub", 5}});
  RecordFile tree = treeOf({{"a/b.epub", 5, 1}});
  const PlanRun run = runPlan(manifest, tree, {"a/b.epub"});
  EXPECT_EQ(run.ops, Ops{download(tree.offsets[0], manifest.offsets[0])});
}

TEST(SyncPlanner, SamePrefixAtACaseOnlyRenameIsAModifyNotANoOp) {
  RecordFile manifest = manifestOf({{"a/b.epub", 5}});
  RecordFile tree = treeOf({{"A/B.EPUB", 5, 1}});
  const PlanRun run = runPlan(manifest, tree, {"a/b.epub"});
  EXPECT_EQ(run.ops, Ops{download(tree.offsets[0], manifest.offsets[0])});
}

TEST(SyncPlanner, EntryBeyondA24BitOffsetIsRefusedAsOverTheCap) {
  const std::string hidden = "." + std::string(MAX_SYNC_PATH_BYTES - 1, 'h');
  constexpr size_t HIDDEN_COUNT = (1u << 24) / (FRAME_HEADER_SIZE + ENTRY_FIXED_BODY_SIZE + MAX_SYNC_PATH_BYTES) + 1;
  std::vector<Entry> entries(HIDDEN_COUNT, Entry{hidden, 1});
  entries.push_back({"late.epub", 2});
  RecordFile manifest = manifestOf({});
  RecordFile tree = treeOf(entries);
  ASSERT_GT(tree.offsets.back(), 0x00FFFFFFu);
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::CapExceeded);
  EXPECT_EQ(run.result.treeEntries, 1u);
  EXPECT_EQ(run.ops, Ops{});
}

TEST(SyncPlanner, TotalIsReportedOnceBeforeTheFirstOperation) {
  MixedScenario scenario;
  const PlanRun run = runPlan(scenario.manifest, scenario.tree, scenario.card);
  EXPECT_EQ(run.totals, std::vector<uint32_t>{static_cast<uint32_t>(run.ops.size())});
  EXPECT_EQ(run.opsBeforeTotal, 0u);
  EXPECT_EQ(run.ops.size(), 5u);
}

TEST(SyncPlanner, EmptyPlanReportsATotalOfZero) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf(LIBRARY);
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.totals, std::vector<uint32_t>{0});
  EXPECT_EQ(run.ops, Ops{});
}

TEST(SyncPlanner, TotalCallbackIsOptional) {
  MixedScenario scenario;
  MemoryRecordStream manifestSource(scenario.manifest.bytes.data(), scenario.manifest.bytes.size(),
                                    scenario.manifest.bytes.size());
  MemoryRecordStream treeSource(scenario.tree.bytes.data(), scenario.tree.bytes.size(), scenario.tree.bytes.size());
  Card card{scenario.card};
  Recorder recorder;
  auto planner = std::make_unique<SyncPlanner>();
  const SyncOpSink sink{&recorder, recordOp, nullptr};
  EXPECT_EQ(planner->plan(manifestSource, treeSource, cardExists, &card, sink).status, SyncPlanStatus::Ok);
  EXPECT_EQ(recorder.ops.size(), 5u);
}

TEST(SyncPlanner, FailedPlanReportsNoTotal) {
  RecordFile manifest = manifestOf(LIBRARY);
  RecordFile tree = treeOf(numbered(MAX_SYNC_FILES + 1, "lib", 1000));
  const PlanRun run = runPlan(manifest, tree);
  EXPECT_EQ(run.result.status, SyncPlanStatus::CapExceeded);
  EXPECT_EQ(run.totals, std::vector<uint32_t>{});
}

}  // namespace
