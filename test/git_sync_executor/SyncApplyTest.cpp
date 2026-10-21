#include <GitTreeParser.h>
#include <SyncPath.h>
#include <SyncPlanner.h>

#include <cstdio>
#include <functional>
#include <set>

#include "SyncExecutorFixture.h"
#include "TreeFixture.h"

namespace {

// Paths journaled in a manifest that may be torn or missing, as an interrupted run leaves it.
std::set<std::string> journaledPaths(MemorySyncStorage& storage) {
  std::set<std::string> paths;
  SyncFile* file = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Read);
  if (!file) return paths;
  auto reader = std::make_unique<RecordReader>(*file, RecordFileKind::Manifest);
  SyncRecord record;
  while (reader->next(record) == RecordStatus::Record) {
    if (record.inJournal && record.kind == RecordFrameKind::Entry) paths.insert(record.path);
  }
  storage.close(file);
  return paths;
}

// The card without the manifest, whose bytes depend on when it was last compacted.
MemorySyncStorage::Snapshot cardWithoutManifest(MemorySyncStorage& storage) {
  auto snap = storage.snapshot();
  snap.files.erase(SYNC_MANIFEST_PATH);
  return snap;
}

// What the device glue does with the trees response: syncable records into tree.tmp.
struct TreeTmpWriter {
  RecordWriter* writer;
  size_t hidden = 0;
  static bool onRecord(void* ctx, const GitTreeRecord& record) {
    auto* self = static_cast<TreeTmpWriter*>(ctx);
    if (classifySyncPath(record.path, record.pathLen) != SyncPathVerdict::Syncable) {
      ++self->hidden;
      return true;
    }
    return self->writer->put(record.sha, record.size, record.path, record.pathLen, NO_OFFSET, nullptr);
  }
};

class SyncApplyTest : public SyncExecutorTest {
 protected:
  // prepare(), then apply() with a fresh planner over the tree.tmp already on the card.
  SyncRunResult runSync() {
    auto executor = makeExecutor();
    if (!executor) return {SyncRunStatus::OutOfMemory};
    const SyncRunResult prepared = executor->prepare();
    if (prepared.status != SyncRunStatus::Ok) return prepared;
    auto planner = makeUniqueNoThrow<SyncPlanner>();
    if (!planner) return {SyncRunStatus::OutOfMemory};
    return executor->apply(*planner);
  }

  void writeTree(const std::vector<Entry>& tree) {
    writeRecords(storage, SYNC_TREE_PATH, RecordFileKind::Tree, tree, nullptr);
    for (const Entry& entry : tree) serve(entry.content);
  }

  struct EndState {
    MemorySyncStorage::Snapshot card;
    std::map<std::string, LiveEntry> live;
    std::vector<std::string> recentBooks;
    std::string openBook;
  };

  EndState endState() { return {cardWithoutManifest(storage), liveEntries(storage), recentBooks, openBook}; }

  void expectSameEndState(const EndState& expected, const std::string& where) {
    EXPECT_EQ(cardWithoutManifest(storage), expected.card) << where;
    EXPECT_EQ(liveEntries(storage), expected.live) << where;
    EXPECT_EQ(recentBooks, expected.recentBooks) << where;
    EXPECT_EQ(openBook, expected.openBook) << where;
  }

  // For every N up to the uninterrupted run's mutating calls and both tears: seed, run with the N-th call failing,
  // then run again; the rerun must end like the uninterrupted run. The hooks see the card after each run.
  // Returns the uninterrupted run's mutating calls.
  size_t sweepEveryFault(const std::function<void()>& seed,
                         const std::function<void(const std::string& where)>& afterInterrupt = nullptr,
                         const std::function<void(const std::string& where)>& afterRerun = nullptr) {
    seed();
    storage.resetCounters();
    const SyncRunResult clean = runSync();
    EXPECT_EQ(clean.status, SyncRunStatus::Ok);
    EXPECT_EQ(clean.fileErrors, 0u);
    const size_t calls = storage.mutatingCalls();
    const EndState expected = endState();

    for (size_t n = 1; n <= calls; ++n) {
      for (const auto tear : {MemorySyncStorage::Tear::None, MemorySyncStorage::Tear::Half}) {
        const std::string where =
            "n=" + std::to_string(n) + (tear == MemorySyncStorage::Tear::Half ? " half" : " none");
        seed();
        storage.resetCounters();
        storage.failAt(n, tear);
        const SyncRunResult interrupted = runSync();
        EXPECT_TRUE(interrupted.status != SyncRunStatus::Ok || interrupted.fileErrors > 0) << where;
        storage.clearFault();
        if (afterInterrupt) afterInterrupt(where);

        const SyncRunResult rerun = runSync();
        EXPECT_EQ(rerun.status, SyncRunStatus::Ok) << where;
        EXPECT_EQ(rerun.fileErrors, 0u) << where;
        expectSameEndState(expected, where);
        EXPECT_EQ(storage.openFiles(), 0u) << where;
        if (afterRerun) afterRerun(where);
      }
    }
    return calls;
  }
};

}  // namespace

TEST_F(SyncApplyTest, ApplyRunsThePlanOnTheCard) {
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"gone.epub", "G"}, {"a.epub", "old"}}, nullptr);
  seedFile(storage, "/gone.epub", "G");
  seedFile(storage, "/a.epub", "old");
  writeTree({{"a.epub", "new"}, {"b/c.txt", "C"}});

  const SyncRunResult result = runSync();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.added, 1u);
  EXPECT_EQ(result.updated, 1u);
  EXPECT_EQ(result.deleted, 1u);
  EXPECT_EQ(fileContent(storage, "/a.epub"), "new");
  EXPECT_EQ(fileContent(storage, "/b/c.txt"), "C");
  EXPECT_FALSE(storage.exists("/gone.epub"));
  const auto live = liveEntries(storage);
  EXPECT_EQ(live.size(), 2u);
  EXPECT_EQ(live.at("a.epub").shaHex, hex(blobSha("new").data()));
  ASSERT_EQ(progress.size(), 3u);
  EXPECT_EQ(progress.back().done, 3u);
  EXPECT_EQ(progress.back().total, 3u);
  EXPECT_EQ(storage.openFiles(), 0u);
}

TEST_F(SyncApplyTest, ApplyBeforePrepareIsRefusedUntouched) {
  writeTree({{"a.epub", "A"}});
  storage.resetCounters();
  auto executor = makeExecutor();
  auto planner = makeUniqueNoThrow<SyncPlanner>();

  EXPECT_EQ(executor->apply(*planner).status, SyncRunStatus::NotPrepared);
  EXPECT_EQ(storage.mutatingCalls(), 0u);
}

TEST_F(SyncApplyTest, MissingOrUnsealedTreeChangesNothing) {
  auto executor = makeExecutor();
  ASSERT_EQ(executor->prepare().status, SyncRunStatus::Ok);
  auto planner = makeUniqueNoThrow<SyncPlanner>();
  EXPECT_EQ(executor->apply(*planner).status, SyncRunStatus::StorageError) << "no tree.tmp";

  writeTree({{"a.epub", "A"}});
  std::string tree = fileContent(storage, SYNC_TREE_PATH);
  tree.resize(tree.size() - 1);
  seedFile(storage, SYNC_TREE_PATH, tree);
  storage.resetCounters();
  executor = makeExecutor();
  ASSERT_EQ(executor->prepare().status, SyncRunStatus::Ok);

  const SyncRunResult result = executor->apply(*planner);

  EXPECT_EQ(result.status, SyncRunStatus::TreeError);
  EXPECT_EQ(result.recordStatus, RecordStatus::Truncated);
  EXPECT_EQ(storage.mutatingCalls(), 0u);
  EXPECT_FALSE(storage.exists("/a.epub"));
}

TEST_F(SyncApplyTest, TreeOverTheCapIsRefusedWithItsCountsBeforeAnyChange) {
  std::vector<Entry> tree;
  tree.reserve(MAX_SYNC_FILES + 1);
  for (size_t i = 0; i <= MAX_SYNC_FILES; ++i) tree.push_back({"b" + std::to_string(i) + ".epub", "x"});
  writeTree(tree);
  ASSERT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);
  storage.resetCounters();

  const SyncRunResult result = runSync();

  EXPECT_EQ(result.status, SyncRunStatus::CapExceeded);
  EXPECT_EQ(result.treeEntries, MAX_SYNC_FILES + 1);
  EXPECT_EQ(result.manifestEntries, 0u);
  EXPECT_EQ(storage.mutatingCalls(), 0u);
}

TEST_F(SyncApplyTest, CancelBeforeApplyStopsWithoutAnyOperation) {
  writeTree({{"a.epub", "A"}});
  auto executor = makeExecutor();
  ASSERT_EQ(executor->prepare().status, SyncRunStatus::Ok);
  auto planner = makeUniqueNoThrow<SyncPlanner>();
  executor->requestCancel();

  EXPECT_EQ(executor->apply(*planner).status, SyncRunStatus::Cancelled);
  EXPECT_FALSE(storage.exists("/a.epub"));
  EXPECT_TRUE(liveEntries(storage).empty());
}

// Every operation kind: a move with its reading state, a modify with a cache, a new nested download, a size-0 file,
// an adopt that matches and one that does not, a delete that empties its folders and one beside a user file, a
// case collision and an unsafe name.
TEST_F(SyncApplyTest, MixedPlanInterruptedAtAnyCallEndsTheSameWhenRunAgain) {
  const auto seed = [&] {
    storage = MemorySyncStorage();
    writeRecords(
        storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest,
        {{"Livres/SF/Dune.epub", "spice"}, {"mod.epub", "old"}, {"Old/Deep/x.epub", "X"}, {"Keep/y.epub", "Y"}},
        nullptr);
    seedFile(storage, "/Livres/SF/Dune.epub", "spice");
    seedFile(storage, cacheDir(BookCacheKind::Epub, "/Livres/SF/Dune.epub") + "/section.bin", "dune epub");
    seedFile(storage, cacheDir(BookCacheKind::Txt, "/Livres/SF/Dune.epub") + "/section.bin", "dune txt");
    seedFile(storage, bookmarkPath("/Livres/SF/Dune.epub"), "dune marks");
    seedFile(storage, "/mod.epub", "old");
    seedFile(storage, cacheDir(BookCacheKind::Epub, "/mod.epub") + "/section.bin", "old layout");
    seedFile(storage, "/adopt.epub", "same");
    seedFile(storage, "/adopt2.epub", "local");
    seedFile(storage, "/Old/Deep/x.epub", "X");
    seedFile(storage, "/Keep/y.epub", "Y");
    seedFile(storage, "/Keep/mine.txt", "user");
    writeTree({{"Lus/Dune.epub", "spice"},
               {"mod.epub", "new content"},
               {"A/B/new.epub", "nested"},
               {"vide.txt", ""},
               {"adopt.epub", "same"},
               {"adopt2.epub", "remote"},
               {"Case.epub", "upper"},
               {"case.epub", "lower"},
               {"bad:name.epub", "unsafe"}});
    recentBooks = {"/Livres/SF/Dune.epub", "/mod.epub"};
    openBook = "/Livres/SF/Dune.epub";
  };

  seed();
  const SyncRunResult clean = runSync();
  ASSERT_EQ(clean.status, SyncRunStatus::Ok);
  EXPECT_EQ(clean.moved, 1u);
  EXPECT_EQ(clean.updated, 1u);
  EXPECT_EQ(clean.added, 4u) << "new nested, size 0, adopt mismatch, case winner";
  EXPECT_EQ(clean.adopted, 1u);
  EXPECT_EQ(clean.deleted, 2u);
  EXPECT_EQ(clean.skippedCaseCollision, 1u);
  EXPECT_EQ(clean.skippedUnsafeName, 1u);
  EXPECT_EQ(fileContent(storage, bookmarkPath("/Lus/Dune.epub")), "dune marks");
  EXPECT_EQ(fileContent(storage, cacheDir(BookCacheKind::Txt, "/Lus/Dune.epub") + "/section.bin"), "dune txt");
  EXPECT_FALSE(storage.exists(cacheDir(BookCacheKind::Epub, "/mod.epub").c_str()));
  EXPECT_FALSE(storage.exists("/Old"));
  EXPECT_EQ(fileContent(storage, "/Keep/mine.txt"), "user");
  EXPECT_EQ(openBook, "/Lus/Dune.epub");
  EXPECT_EQ(fetches.count(hex(blobSha("").data())), 0u);

  EXPECT_GT(sweepEveryFault(seed), 40u);
}

TEST_F(SyncApplyTest, AdoptOnlyFirstSyncInterruptedAtAnyCallNeverHashesAnAdoptedFileAgain) {
  std::vector<Entry> library;
  for (int i = 0; i < 6; ++i)
    library.push_back({"Livres/b" + std::to_string(i) + ".epub", "book " + std::to_string(i)});
  const auto seed = [&] {
    storage = MemorySyncStorage();
    for (const Entry& book : library) seedFile(storage, "/" + book.path, book.content);
    writeTree(library);
  };
  seed();
  hashed.clear();
  ASSERT_EQ(runSync().adopted, library.size());
  EXPECT_TRUE(fetches.empty());

  std::set<std::string> journaled;
  size_t interruptsWithAdoptions = 0;
  const size_t calls = sweepEveryFault(
      seed,
      [&](const std::string&) {
        journaled = journaledPaths(storage);
        if (!journaled.empty()) ++interruptsWithAdoptions;
        hashed.clear();
      },
      [&](const std::string& where) {
        for (const std::string& path : hashed) {
          EXPECT_EQ(journaled.count(path.substr(1)), 0u) << path << " hashed again, " << where;
        }
        EXPECT_EQ(hashed.size(), library.size() - journaled.size()) << where;
      });

  EXPECT_GE(calls, library.size()) << "one journal frame per adoption at least";
  EXPECT_GT(interruptsWithAdoptions, 0u);
  EXPECT_TRUE(fetches.empty());
}

// Each run's manifest writes stay linear: one frame per operation, plus one compacted base when a journal exists.
TEST_F(SyncApplyTest, FirstSyncAtTheCapAdoptsEverythingWithLinearManifestWrites) {
  std::vector<Entry> library;
  library.reserve(MAX_SYNC_FILES);
  uint64_t entryFrames = 0;
  for (size_t i = 0; i < MAX_SYNC_FILES; ++i) {
    char path[48];
    snprintf(path, sizeof(path), "Livres/Auteur %02zu/Livre %04zu.epub", i % 50, i);
    library.push_back({path, "content of book " + std::to_string(i)});
    entryFrames += FRAME_HEADER_SIZE + ENTRY_FIXED_BODY_SIZE + strlen(path);
  }
  for (const Entry& book : library) seedFile(storage, "/" + book.path, book.content);
  writeTree(library);
  storage.resetCounters();

  const SyncRunResult first = runSync();
  const uint64_t firstBytes = storage.bytesWritten();

  EXPECT_EQ(first.status, SyncRunStatus::Ok);
  EXPECT_EQ(first.adopted, MAX_SYNC_FILES);
  EXPECT_EQ(first.fileErrors, 0u);
  EXPECT_TRUE(fetches.empty());
  EXPECT_LE(firstBytes, EMPTY_MANIFEST_SIZE + entryFrames + 1024);

  storage.resetCounters();
  progress.clear();
  const SyncRunResult second = runSync();
  const uint64_t secondBytes = storage.bytesWritten();
  const uint64_t compactedBase = EMPTY_MANIFEST_SIZE + entryFrames;

  EXPECT_EQ(second.status, SyncRunStatus::Ok);
  EXPECT_TRUE(progress.empty());
  EXPECT_LE(secondBytes, compactedBase + 1024);

  storage.resetCounters();
  EXPECT_EQ(runSync().status, SyncRunStatus::Ok);
  const uint64_t thirdBytes = storage.bytesWritten();
  EXPECT_EQ(thirdBytes, 0u);

  printf("[ cap ] %zu files: first sync %llu B (bound %llu B), no-change run %llu B (bound %llu B), then %llu B\n",
         MAX_SYNC_FILES, static_cast<unsigned long long>(firstBytes),
         static_cast<unsigned long long>(EMPTY_MANIFEST_SIZE + entryFrames + 1024),
         static_cast<unsigned long long>(secondBytes), static_cast<unsigned long long>(compactedBase + 1024),
         static_cast<unsigned long long>(thirdBytes));
  RecordProperty("first_sync_bytes", std::to_string(firstBytes));
  RecordProperty("no_change_bytes", std::to_string(secondBytes));
}

TEST_F(SyncApplyTest, FilesOutsideTheManifestAndUnderCrosspointAreNeverTouched) {
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest,
               {{"Livres/a.epub", "old"}, {"Livres/move.epub", "M"}, {"Livres/gone.epub", "G"}}, nullptr);
  seedFile(storage, "/Livres/a.epub", "old");
  seedFile(storage, "/Livres/move.epub", "M");
  seedFile(storage, "/Livres/gone.epub", "G");
  seedReadingState(storage, "/Livres/move.epub");
  seedReadingState(storage, "/Livres/a.epub");
  const std::vector<Entry> seeded = {
      {"/mine.epub", "user book"},
      {"/Livres/perso.epub", "user book in a synced folder"},
      {"/.crosspoint/settings.json", "{}"},
      {cacheDir(BookCacheKind::Epub, "/other.epub") + "/section.bin", "other"},
      {bookmarkPath("/other.epub"), "other marks"},
  };
  for (const Entry& file : seeded) seedFile(storage, file.path, file.content);
  writeTree({{"Livres/a.epub", "new"},
             {"Lus/move.epub", "M"},
             {"Livres/b.epub", "B"},
             {".crosspoint/evil.bin", "evil"},
             {".github/x", "x"}});
  const auto before = storage.snapshot();

  const SyncRunResult result = runSync();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.added, 1u);
  EXPECT_EQ(result.updated, 1u);
  EXPECT_EQ(result.moved, 1u);
  EXPECT_EQ(result.deleted, 1u);
  for (const Entry& file : seeded) EXPECT_EQ(fileContent(storage, file.path), file.content) << file.path;
  EXPECT_FALSE(storage.exists("/.github"));
  EXPECT_FALSE(storage.exists("/.crosspoint/evil.bin"));
  EXPECT_EQ(fetches.count(hex(blobSha("evil").data())), 0u);
  EXPECT_EQ(fetches.count(hex(blobSha("x").data())), 0u);

  std::set<std::string> allowed;
  for (const char* book : {"/Livres/move.epub", "/Lus/move.epub", "/Livres/a.epub"}) {
    for (const BookCacheKind kind : CACHE_KINDS) allowed.insert(cacheDir(kind, book));
    allowed.insert(bookmarkPath(book));
  }
  const auto isAllowed = [&](const std::string& path) {
    if (path.compare(0, strlen(SYNC_WORK_DIR), SYNC_WORK_DIR) == 0) return true;
    for (const std::string& prefix : allowed) {
      if (path == prefix || path.compare(0, prefix.size() + 1, prefix + "/") == 0) return true;
    }
    return false;
  };
  const auto after = storage.snapshot();
  std::set<std::string> changed;
  for (const auto& [path, content] : before.files) {
    const auto it = after.files.find(path);
    if (it == after.files.end() || it->second != content) changed.insert(path);
  }
  for (const auto& [path, content] : after.files) {
    if (!before.files.count(path)) changed.insert(path);
  }
  for (const auto& dirs : {std::make_pair(&before.directories, &after.directories),
                           std::make_pair(&after.directories, &before.directories)}) {
    for (const std::string& dir : *dirs.first) {
      if (!dirs.second->count(dir)) changed.insert(dir);
    }
  }
  for (const std::string& path : changed) {
    if (path.compare(0, strlen("/.crosspoint"), "/.crosspoint") == 0) {
      EXPECT_TRUE(isAllowed(path)) << path;
    } else {
      EXPECT_TRUE(path.compare(0, strlen("/Livres/"), "/Livres/") == 0 || path.compare(0, 4, "/Lus") == 0) << path;
      EXPECT_NE(path, "/Livres/perso.epub");
    }
  }
}

TEST_F(SyncApplyTest, BadOrFailedDownloadLeavesNoTargetNoEntryAndIsFetchedAgain) {
  const std::string content = "the real book";
  const std::string key = hex(blobSha(content).data());
  for (const FetchMode mode :
       {FetchMode::WrongBytes, FetchMode::ShortBody, FetchMode::ExtraBytes, FetchMode::FailMidStream}) {
    storage = MemorySyncStorage();
    fetches.clear();
    writeTree({{"Livres/new.epub", content}});
    fetchMode = mode;

    const SyncRunResult bad = runSync();

    EXPECT_EQ(bad.status, SyncRunStatus::Ok);
    EXPECT_EQ(bad.fileErrors, 1u);
    EXPECT_FALSE(storage.exists("/Livres/new.epub"));
    EXPECT_TRUE(liveEntries(storage).empty());
    EXPECT_EQ(fetches[key], 1);

    fetchMode = FetchMode::Serve;
    const SyncRunResult rerun = runSync();

    EXPECT_FALSE(storage.exists(SYNC_DOWNLOAD_TMP_PATH));
    EXPECT_EQ(rerun.status, SyncRunStatus::Ok);
    EXPECT_EQ(rerun.added, 1u);
    EXPECT_EQ(fetches[key], 2);
    EXPECT_EQ(fileContent(storage, "/Livres/new.epub"), content);
    EXPECT_EQ(liveEntries(storage).at("Livres/new.epub").shaHex, key);
  }
}

// The planner sees the file at the destination and the source gone, and plans the move's resume.
TEST_F(SyncApplyTest, MoveInterruptedAfterItsFileRenameMigratesTheReadingStateOnTheNextRun) {
  const auto seed = [&] {
    storage = MemorySyncStorage();
    writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/SF/Dune.epub", "spice"}}, nullptr);
    seedFile(storage, "/Livres/SF/Dune.epub", "spice");
    seedReadingState(storage, "/Livres/SF/Dune.epub");
    writeTree({{"Lus/Dune.epub", "spice"}});
    recentBooks = {"/Livres/SF/Dune.epub"};
    openBook = "/Livres/SF/Dune.epub";
  };
  seed();
  storage.resetCounters();
  ASSERT_EQ(runSync().status, SyncRunStatus::Ok);
  const auto& log = storage.calls();
  const auto fileRename = std::find(
      log.begin(), log.end(),
      MemorySyncStorage::Call{MemorySyncStorage::CallKind::Rename, "/Livres/SF/Dune.epub", "/Lus/Dune.epub", true});
  ASSERT_NE(fileRename, log.end());
  const size_t failAt = static_cast<size_t>(fileRename - log.begin()) + 2;

  seed();
  storage.resetCounters();
  storage.failAt(failAt, MemorySyncStorage::Tear::None);
  EXPECT_EQ(runSync().status, SyncRunStatus::StorageError);
  storage.clearFault();
  EXPECT_EQ(fileContent(storage, "/Lus/Dune.epub"), "spice");
  EXPECT_FALSE(storage.exists("/Livres/SF/Dune.epub"));
  EXPECT_TRUE(storage.exists(cacheDir(BookCacheKind::Epub, "/Livres/SF/Dune.epub").c_str()));
  EXPECT_EQ(openBook, "/Livres/SF/Dune.epub");
  progress.clear();

  const SyncRunResult rerun = runSync();

  EXPECT_EQ(rerun.status, SyncRunStatus::Ok);
  EXPECT_EQ(rerun.moved, 1u);
  ASSERT_FALSE(progress.empty());
  EXPECT_EQ(progress[0].kind, SyncOpKind::Move);
  for (const BookCacheKind kind : CACHE_KINDS) {
    EXPECT_EQ(fileContent(storage, cacheDir(kind, "/Lus/Dune.epub") + "/section.bin"), "/Livres/SF/Dune.epub");
    EXPECT_FALSE(storage.exists(cacheDir(kind, "/Livres/SF/Dune.epub").c_str()));
  }
  EXPECT_EQ(fileContent(storage, bookmarkPath("/Lus/Dune.epub")), "/Livres/SF/Dune.epub");
  EXPECT_FALSE(storage.exists(bookmarkPath("/Livres/SF/Dune.epub").c_str()));
  EXPECT_FALSE(storage.exists("/Livres"));
  EXPECT_EQ(recentBooks, std::vector<std::string>{"/Lus/Dune.epub"});
  EXPECT_EQ(openBook, "/Lus/Dune.epub");
  EXPECT_TRUE(fetches.empty());
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.count("Lus/Dune.epub"), 1u);
}

TEST_F(SyncApplyTest, ModifyInterruptedAfterItsTargetRenameClearsTheCacheOnTheNextRun) {
  const auto seed = [&] {
    storage = MemorySyncStorage();
    writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "old"}}, nullptr);
    seedFile(storage, "/a.epub", "old");
    writeTree({{"a.epub", "brand new"}});
  };
  seed();
  storage.resetCounters();
  ASSERT_EQ(runSync().status, SyncRunStatus::Ok);
  const auto& log = storage.calls();
  const auto targetRename =
      std::find(log.begin(), log.end(),
                MemorySyncStorage::Call{MemorySyncStorage::CallKind::Rename, SYNC_DOWNLOAD_TMP_PATH, "/a.epub", true});
  ASSERT_NE(targetRename, log.end());
  const size_t failAt = static_cast<size_t>(targetRename - log.begin()) + 2;

  seed();
  storage.resetCounters();
  storage.failAt(failAt, MemorySyncStorage::Tear::Half);
  EXPECT_EQ(runSync().status, SyncRunStatus::StorageError);
  storage.clearFault();
  EXPECT_EQ(fileContent(storage, "/a.epub"), "brand new");
  // The reader opened the new file through the old cache before the next run.
  seedFile(storage, cacheDir(BookCacheKind::Epub, "/a.epub") + "/section.bin", "old layout");
  cleared.clear();

  const SyncRunResult rerun = runSync();

  EXPECT_EQ(rerun.status, SyncRunStatus::Ok);
  EXPECT_EQ(rerun.updated, 1u);
  EXPECT_EQ(cleared, std::vector<std::string>{"/a.epub"});
  EXPECT_FALSE(storage.exists(cacheDir(BookCacheKind::Epub, "/a.epub").c_str()));
  EXPECT_EQ(fileContent(storage, "/a.epub"), "brand new");
  EXPECT_EQ(liveEntries(storage).at("a.epub").shaHex, hex(blobSha("brand new").data()));
}

// From a recorded trees response, through the parser, the path filter, tree.tmp and the planner, to the card.
TEST_F(SyncApplyTest, RecordedTreeResponseEndsOnTheCardAndARerunDoesNothing) {
  for (const TreeFixtureBlob& blob : TREE_FIXTURE_BLOBS) serve(blob.content);
  {
    ASSERT_TRUE(storage.mkdirs(SYNC_WORK_DIR));
    SyncFile* file = storage.open(SYNC_TREE_PATH, SyncOpenMode::Truncate);
    ASSERT_NE(file, nullptr);
    auto writer = makeUniqueNoThrow<RecordWriter>(*file);
    ASSERT_NE(writer, nullptr);
    ASSERT_TRUE(writer->begin(RecordFileKind::Tree));
    TreeTmpWriter glue{writer.get()};
    auto parser = makeUniqueNoThrow<GitTreeParser>(&glue, &TreeTmpWriter::onRecord);
    ASSERT_NE(parser, nullptr);
    const std::string body = TREE_FIXTURE_BODY;
    for (size_t pos = 0; pos < body.size(); pos += 7)
      parser->feed(body.data() + pos, std::min<size_t>(7, body.size() - pos));
    ASSERT_TRUE(parser->isComplete());
    ASSERT_FALSE(parser->isTruncated());
    ASSERT_TRUE(writer->seal());
    EXPECT_EQ(glue.hidden, 1u);
    storage.close(file);
  }

  const SyncRunResult result = runSync();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.added, 5u);
  EXPECT_EQ(result.fileErrors, 0u);
  std::map<std::string, std::string> cardFiles;
  for (const auto& [path, content] : storage.snapshot().files) {
    if (path.compare(0, strlen("/.crosspoint/"), "/.crosspoint/") != 0) cardFiles[path] = content;
  }
  std::map<std::string, std::string> expectedFiles;
  std::map<std::string, LiveEntry> expectedLive;
  for (const TreeFixtureBlob& blob : TREE_FIXTURE_BLOBS) {
    if (blob.path[0] == '.') {
      EXPECT_EQ(fetches.count(blob.shaHex), 0u) << blob.path;
      continue;
    }
    expectedFiles[std::string("/") + blob.path] = blob.content;
    expectedLive[blob.path] = {blob.shaHex, blob.size};
    EXPECT_EQ(fetches.count(blob.shaHex), blob.size == 0 ? 0u : 1u) << blob.path;
  }
  EXPECT_EQ(cardFiles, expectedFiles);
  EXPECT_TRUE(storage.exists("/Livres/vide.txt"));
  EXPECT_FALSE(storage.exists("/.atelier"));
  EXPECT_EQ(liveEntries(storage), expectedLive);

  auto executor = makeExecutor();
  ASSERT_EQ(executor->prepare().status, SyncRunStatus::Ok);
  const auto card = storage.snapshot();
  storage.resetCounters();
  progress.clear();
  fetches.clear();
  auto planner = makeUniqueNoThrow<SyncPlanner>();

  const SyncRunResult rerun = executor->apply(*planner);

  EXPECT_EQ(rerun.status, SyncRunStatus::Ok);
  EXPECT_TRUE(progress.empty());
  EXPECT_TRUE(fetches.empty());
  EXPECT_EQ(storage.mutatingCalls(), 0u);
  EXPECT_EQ(storage.snapshot(), card);
  EXPECT_EQ(liveEntries(storage), expectedLive);
}
