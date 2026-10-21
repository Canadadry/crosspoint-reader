#include "SyncExecutorFixture.h"

TEST_F(SyncExecutorTest, FirstPrepareCreatesAnEmptyManifest) {
  auto executor = makeExecutor();
  ASSERT_NE(executor, nullptr);

  const SyncRunResult result = executor->prepare();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  uint64_t size = 0;
  ASSERT_TRUE(storage.fileSize(SYNC_MANIFEST_PATH, size));
  EXPECT_EQ(size, EMPTY_MANIFEST_SIZE);
  EXPECT_TRUE(liveEntries(storage).empty());
  EXPECT_FALSE(storage.exists(SYNC_MANIFEST_NEW_PATH));
}

TEST_F(SyncExecutorTest, PrepareOverAManifestWithoutJournalWritesNothing) {
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "A"}}, nullptr);
  const auto before = storage.snapshot();
  storage.resetCounters();

  EXPECT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);

  EXPECT_EQ(storage.mutatingCalls(), 0u);
  EXPECT_EQ(storage.snapshot(), before);
}

TEST_F(SyncExecutorTest, CorruptOrWrongVersionManifestStopsBeforeAnyChange) {
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "A"}}, nullptr);
  std::string bytes = fileContent(storage, SYNC_MANIFEST_PATH);
  struct Case {
    size_t index;
    RecordStatus expected;
  };
  for (const Case c : {Case{20, RecordStatus::Corrupt}, Case{4, RecordStatus::WrongVersion}}) {
    std::string damaged = bytes;
    damaged[c.index] ^= 0x5A;
    seedFile(storage, SYNC_MANIFEST_PATH, damaged);
    const auto before = storage.snapshot();
    storage.resetCounters();

    const SyncRunResult result = makeExecutor()->prepare();

    EXPECT_EQ(result.status, SyncRunStatus::ManifestError);
    EXPECT_EQ(result.recordStatus, c.expected);
    EXPECT_EQ(storage.mutatingCalls(), 0u);
    EXPECT_EQ(storage.snapshot(), before);
  }
}

TEST_F(SyncExecutorTest, PrepareRemovesALeftoverDownload) {
  ASSERT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);
  seedFile(storage, SYNC_DOWNLOAD_TMP_PATH, "partial");

  EXPECT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);

  EXPECT_FALSE(storage.exists(SYNC_DOWNLOAD_TMP_PATH));
}

TEST_F(SyncExecutorTest, LeftoverRewriteBesideAManifestIsRemoved) {
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "A"}}, nullptr);
  writeRecords(storage, SYNC_MANIFEST_NEW_PATH, RecordFileKind::Manifest, {{"b.epub", "B"}}, nullptr);

  EXPECT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);

  EXPECT_FALSE(storage.exists(SYNC_MANIFEST_NEW_PATH));
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.count("a.epub"), 1u);
}

TEST_F(SyncExecutorTest, SealedRewriteWithoutAManifestIsPromoted) {
  writeRecords(storage, SYNC_MANIFEST_NEW_PATH, RecordFileKind::Manifest, {{"b.epub", "B"}}, nullptr);

  EXPECT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);

  EXPECT_FALSE(storage.exists(SYNC_MANIFEST_NEW_PATH));
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.count("b.epub"), 1u);
}

TEST_F(SyncExecutorTest, UnsealedRewriteWithoutAManifestGivesAnEmptyManifest) {
  writeRecords(storage, SYNC_MANIFEST_NEW_PATH, RecordFileKind::Manifest, {{"b.epub", "B"}}, nullptr);
  const std::string bytes = fileContent(storage, SYNC_MANIFEST_NEW_PATH);
  for (const std::string& garbage : {bytes.substr(0, bytes.size() - 3), std::string("junk")}) {
    ASSERT_TRUE(storage.remove(SYNC_MANIFEST_PATH) || !storage.exists(SYNC_MANIFEST_PATH));
    seedFile(storage, SYNC_MANIFEST_NEW_PATH, garbage);

    EXPECT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);

    EXPECT_FALSE(storage.exists(SYNC_MANIFEST_NEW_PATH));
    EXPECT_TRUE(liveEntries(storage).empty());
  }
}

TEST_F(SyncExecutorTest, PrepareFoldsTheJournalIntoAFreshBase) {
  std::vector<uint32_t> offsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "A"}, {"b.epub", "B"}}, &offsets);
  {
    SyncFile* file = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Append);
    ASSERT_NE(file, nullptr);
    auto writer = std::make_unique<RecordWriter>(*file);
    ASSERT_TRUE(writer->appendTo(static_cast<uint32_t>(file->size())));
    ASSERT_TRUE(writer->drop(offsets[0]));
    const Sha sha = blobSha("C");
    ASSERT_TRUE(writer->put(sha.data(), 1, "c.epub", 6, NO_OFFSET, nullptr));
    storage.close(file);
  }
  const std::string torn = fileContent(storage, SYNC_MANIFEST_PATH) + "P\x01";
  seedFile(storage, SYNC_MANIFEST_PATH, torn);
  const auto expected = liveEntries(storage);

  EXPECT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);

  EXPECT_EQ(liveEntries(storage), expected);
  EXPECT_EQ(expected.size(), 2u);
  EXPECT_FALSE(storage.exists(SYNC_MANIFEST_NEW_PATH));
  EXPECT_EQ(journalFrames(storage), 0u);
}

TEST_F(SyncExecutorTest, TornTailAloneIsCompactedAway) {
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "A"}}, nullptr);
  const std::string clean = fileContent(storage, SYNC_MANIFEST_PATH);
  seedFile(storage, SYNC_MANIFEST_PATH, clean + "D");

  EXPECT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);

  EXPECT_EQ(fileContent(storage, SYNC_MANIFEST_PATH), clean);
}

TEST_F(SyncExecutorTest, NewDownloadLandsVerifiedAndIsJournaled) {
  auto executor = begin({{"Livres/SF/Dune.epub", "spice must flow"}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.added, 1u);
  EXPECT_EQ(fileContent(storage, "/Livres/SF/Dune.epub"), "spice must flow");
  EXPECT_FALSE(storage.exists(SYNC_DOWNLOAD_TMP_PATH));
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.at("Livres/SF/Dune.epub"), (LiveEntry{hex(blobSha("spice must flow").data()), 15}));
}

TEST_F(SyncExecutorTest, ModifyReplacesTheFileItsEntryAndClearsTheCache) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "old"}}, &manifestOffsets);
  seedFile(storage, "/a.epub", "old");
  auto executor = begin({{"a.epub", "brand new"}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, manifestOffsets[0], treeOffsets[0])),
            SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.updated, 1u);
  EXPECT_EQ(result.added, 0u);
  EXPECT_EQ(fileContent(storage, "/a.epub"), "brand new");
  EXPECT_EQ(cleared, std::vector<std::string>{"/a.epub"});
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.at("a.epub").shaHex, hex(blobSha("brand new").data()));
}

TEST_F(SyncExecutorTest, CaseOnlyModifyLandsUnderTheNewNameAndClearsBothCaches) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "old"}}, &manifestOffsets);
  seedFile(storage, "/a.epub", "old");
  auto executor = begin({{"A.epub", "brand new"}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, manifestOffsets[0], treeOffsets[0])),
            SyncOpOutcome::Applied);
  executor->end();

  const auto files = storage.snapshot().files;
  EXPECT_EQ(files.count("/a.epub"), 0u);
  EXPECT_EQ(fileContent(storage, "/A.epub"), "brand new");
  EXPECT_EQ(cleared, (std::vector<std::string>{"/a.epub", "/A.epub"}));
}

TEST_F(SyncExecutorTest, NewDownloadClearsAStaleCacheAtItsPathAndOverwritesALocalFile) {
  seedFile(storage, "/b.txt", "local copy");
  auto executor = begin({{"b.txt", "remote"}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::Applied);
  executor->end();

  EXPECT_EQ(fileContent(storage, "/b.txt"), "remote");
  EXPECT_EQ(cleared, std::vector<std::string>{"/b.txt"});
}

TEST_F(SyncExecutorTest, EmptyFileIsCreatedWithoutAFetch) {
  auto executor = begin({{"Livres/vide.txt", ""}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::Applied);
  executor->end();

  EXPECT_EQ(fileContent(storage, "/Livres/vide.txt"), "");
  EXPECT_TRUE(fetches.empty());
  EXPECT_EQ(liveEntries(storage).count("Livres/vide.txt"), 1u);
}

TEST_F(SyncExecutorTest, BadBytesNeverLandAndAreRetriedByTheNextRun) {
  for (const FetchMode mode : {FetchMode::WrongBytes, FetchMode::ShortBody, FetchMode::ExtraBytes}) {
    MemorySyncStorage fresh;
    storage = std::move(fresh);
    fetches.clear();
    fetchMode = mode;
    auto executor = begin({{"Livres/a.epub", "twelve bytes"}});

    EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::VerifyFailed);
    const SyncRunResult result = executor->end();

    EXPECT_EQ(result.status, SyncRunStatus::Ok);
    EXPECT_EQ(result.fileErrors, 1u);
    EXPECT_EQ(result.added, 0u);
    EXPECT_FALSE(storage.exists("/Livres/a.epub"));
    EXPECT_FALSE(storage.exists(SYNC_DOWNLOAD_TMP_PATH));
    EXPECT_TRUE(liveEntries(storage).empty());

    fetchMode = FetchMode::Serve;
    executor = begin({{"Livres/a.epub", "twelve bytes"}});
    EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::Applied);
    executor->end();
    EXPECT_EQ(fileContent(storage, "/Livres/a.epub"), "twelve bytes");
    EXPECT_EQ(fetches[hex(blobSha("twelve bytes").data())], 2);
  }
}

TEST_F(SyncExecutorTest, FetchFailingMidStreamLeavesNoPartialFile) {
  seedFile(storage, "/a.epub", "keep me");
  fetchMode = FetchMode::FailMidStream;
  auto executor = begin({{"a.epub", "a longer remote body"}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::FetchFailed);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.fileErrors, 1u);
  EXPECT_EQ(fileContent(storage, "/a.epub"), "keep me");
  EXPECT_FALSE(storage.exists(SYNC_DOWNLOAD_TMP_PATH));
  EXPECT_TRUE(liveEntries(storage).empty());
}

TEST_F(SyncExecutorTest, TargetBlockedByAFolderOrAFileIsAPerFileErrorAndTheRunGoesOn) {
  ASSERT_TRUE(storage.mkdirs("/a.epub"));
  seedFile(storage, "/Livres", "a file where a folder should be");
  auto executor = begin({{"a.epub", "A"}, {"Livres/b.epub", "B"}, {"c.epub", "C"}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])),
            SyncOpOutcome::TargetUnwritable);
  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[1])),
            SyncOpOutcome::TargetUnwritable);
  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[2])), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.fileErrors, 2u);
  EXPECT_EQ(result.added, 1u);
  EXPECT_TRUE(storage.isEmptyDirectory("/a.epub"));
  EXPECT_FALSE(storage.exists(SYNC_DOWNLOAD_TMP_PATH));
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.count("c.epub"), 1u);
}

TEST_F(SyncExecutorTest, FailingWorkDirectoryOrJournalIsFatalAndStopsTheRun) {
  // Calls of one root download: temp truncate, temp write, rename, journal write.
  for (const size_t failing : {size_t{1}, size_t{2}, size_t{4}}) {
    MemorySyncStorage fresh;
    storage = std::move(fresh);
    auto executor = begin({{"a.epub", "A"}, {"b.epub", "B"}});
    storage.resetCounters();
    storage.failAt(failing, MemorySyncStorage::Tear::None);

    EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::Fatal);
    const size_t callsAtFailure = storage.mutatingCalls();
    EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[1])), SyncOpOutcome::Fatal);
    const SyncRunResult result = executor->end();

    EXPECT_EQ(storage.mutatingCalls(), callsAtFailure) << "nothing runs after a fatal outcome";
    EXPECT_EQ(result.status, SyncRunStatus::StorageError);
    EXPECT_EQ(result.added, 0u);
    storage.clearFault();
    EXPECT_TRUE(liveEntries(storage).empty());
  }
}

TEST_F(SyncExecutorTest, FailingCacheClearHookIsFatalBeforeTheTargetIsTouched) {
  seedFile(storage, "/a.epub", "local");
  auto executor = begin({{"a.epub", "A"}});
  clearResult = false;

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::Fatal);
  EXPECT_EQ(executor->end().status, SyncRunStatus::StorageError);

  EXPECT_EQ(fileContent(storage, "/a.epub"), "local");
}

TEST_F(SyncExecutorTest, OperationsBeforeBeginAndBeginBeforePrepareAreRefused) {
  auto executor = makeExecutor();
  EXPECT_FALSE(executor->begin());
  EXPECT_EQ(executor->end().status, SyncRunStatus::NotPrepared);

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, RECORD_HEADER_SIZE)), SyncOpOutcome::Fatal);
  EXPECT_EQ(storage.mutatingCalls(), 0u);
}

TEST_F(SyncExecutorTest, MatchingLocalFileIsAdoptedWithoutFetchOrCacheClear) {
  seedFile(storage, "/Livres/a.epub", "from my PC");
  auto executor = begin({{"Livres/a.epub", "from my PC"}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::AdoptOrDownload, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.adopted, 1u);
  EXPECT_EQ(result.added, 0u);
  EXPECT_EQ(hashed, std::vector<std::string>{"/Livres/a.epub"});
  EXPECT_TRUE(fetches.empty());
  EXPECT_TRUE(cleared.empty());
  EXPECT_EQ(fileContent(storage, "/Livres/a.epub"), "from my PC");
  EXPECT_EQ(liveEntries(storage).at("Livres/a.epub").shaHex, hex(blobSha("from my PC").data()));
}

TEST_F(SyncExecutorTest, DifferentLocalFileIsOverwrittenAndASizeMismatchSkipsTheHash) {
  seedFile(storage, "/same.epub", "local AAAA");
  seedFile(storage, "/longer.epub", "local, longer than remote");
  seedFile(storage, "/unknown.epub", "remote");
  auto executor = begin({{"same.epub", "remote BBB"}, {"longer.epub", "remote"}, {"unknown.epub", "remote"}});
  unhashable = "/unknown.epub";

  for (const uint32_t offset : treeOffsets) {
    EXPECT_EQ(executor->execute(makeOp(SyncOpKind::AdoptOrDownload, NO_OFFSET, offset)), SyncOpOutcome::Applied);
  }
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.adopted, 0u);
  EXPECT_EQ(result.added, 3u);
  EXPECT_EQ(hashed, (std::vector<std::string>{"/same.epub", "/unknown.epub"}));
  EXPECT_EQ(fileContent(storage, "/same.epub"), "remote BBB");
  EXPECT_EQ(fileContent(storage, "/longer.epub"), "remote");
  EXPECT_EQ(fileContent(storage, "/unknown.epub"), "remote");
  EXPECT_EQ(liveEntries(storage).size(), 3u);
}

TEST_F(SyncExecutorTest, DeleteRemovesTheFileAndEveryFolderItEmptied) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest,
               {{"Livres/SF/Dune.epub", "D"}, {"Lus/b.epub", "B"}, {"c.epub", "C"}}, &manifestOffsets);
  seedFile(storage, "/Livres/SF/Dune.epub", "D");
  seedFile(storage, "/Lus/b.epub", "B");
  seedFile(storage, "/Lus/mine.txt", "user file");
  seedFile(storage, "/c.epub", "C");
  auto executor = begin({});

  for (const uint32_t offset : manifestOffsets) {
    EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Delete, offset, NO_OFFSET)), SyncOpOutcome::Applied);
  }
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.deleted, 3u);
  EXPECT_EQ(result.pruned, 2u);
  const auto snap = storage.snapshot();
  EXPECT_EQ(snap.files.count("/Livres/SF/Dune.epub"), 0u);
  EXPECT_EQ(snap.files.count("/c.epub"), 0u);
  EXPECT_EQ(snap.directories.count("/Livres/SF"), 0u);
  EXPECT_EQ(snap.directories.count("/Livres"), 0u);
  EXPECT_EQ(fileContent(storage, "/Lus/mine.txt"), "user file");
  EXPECT_TRUE(cleared.empty()) << "a deleted book keeps its reading state";
  EXPECT_TRUE(liveEntries(storage).empty());
}

TEST_F(SyncExecutorTest, DeleteOfAnAlreadyMissingFileStillCompletes) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/a.epub", "A"}}, &manifestOffsets);
  ASSERT_TRUE(storage.mkdirs("/Livres"));
  auto executor = begin({});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Delete, manifestOffsets[0], NO_OFFSET)), SyncOpOutcome::Applied);
  executor->end();

  EXPECT_FALSE(storage.exists("/Livres"));
  EXPECT_TRUE(liveEntries(storage).empty());
}

TEST_F(SyncExecutorTest, DeleteOfAFolderIsAPerFileError) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "A"}}, &manifestOffsets);
  ASSERT_TRUE(storage.mkdirs("/a.epub"));
  auto executor = begin({});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Delete, manifestOffsets[0], NO_OFFSET)),
            SyncOpOutcome::TargetUnwritable);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.fileErrors, 1u);
  EXPECT_TRUE(storage.exists("/a.epub"));
  EXPECT_EQ(liveEntries(storage).size(), 1u);
}

TEST_F(SyncExecutorTest, PruneRemovesEmptyAncestorsUpToTheFirstNonEmptyOne) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"A/B/C/x.epub", "X"}}, &manifestOffsets);
  ASSERT_TRUE(storage.mkdirs("/A/B"));
  seedFile(storage, "/A/keep.txt", "user file");
  auto executor = begin({});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Prune, manifestOffsets[0], NO_OFFSET)), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.pruned, 1u);
  EXPECT_FALSE(storage.exists("/A/B"));
  EXPECT_TRUE(storage.exists("/A/keep.txt"));
  EXPECT_EQ(liveEntries(storage).size(), 1u) << "a prune writes no journal frame";
}

TEST_F(SyncExecutorTest, PathsOutsideTheSyncedSetAreRefusedUntouched) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest,
               {{".github/x", "X"}, {"a/../settings.json", "S"}, {"ok.epub", "O"}}, &manifestOffsets);
  seedFile(storage, "/.github/x", "X");
  seedFile(storage, "/settings.json", "S");
  auto executor = begin({{".crosspoint/evil.bin", "evil"}, {"Livres/.hidden", "H"}});
  const auto before = storage.snapshot();
  storage.resetCounters();

  const SyncOp ops[] = {
      makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0]),
      makeOp(SyncOpKind::AdoptOrDownload, NO_OFFSET, treeOffsets[1]),
      makeOp(SyncOpKind::Download, manifestOffsets[2], treeOffsets[0]),
      makeOp(SyncOpKind::Delete, manifestOffsets[0], NO_OFFSET),
      makeOp(SyncOpKind::Delete, manifestOffsets[1], NO_OFFSET),
      makeOp(SyncOpKind::Prune, manifestOffsets[1], NO_OFFSET),
      moveOp(manifestOffsets[2], treeOffsets[0], true, false),
      moveOp(manifestOffsets[0], treeOffsets[1], true, false),
  };
  for (const SyncOp& op : ops) EXPECT_EQ(executor->execute(op), SyncOpOutcome::RefusedPath);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.fileErrors, 8u);
  EXPECT_TRUE(moves.empty());
  EXPECT_EQ(storage.mutatingCalls(), 0u);
  EXPECT_EQ(storage.snapshot(), before);
  EXPECT_TRUE(fetches.empty());
  EXPECT_TRUE(cleared.empty());
}

TEST_F(SyncExecutorTest, SkipsAreCountedByReasonAndTouchNothing) {
  auto executor = begin({{"a.epub", "A"}, {"b:c.epub", "B"}});
  storage.resetCounters();

  SyncOp collision = makeOp(SyncOpKind::Skip, NO_OFFSET, treeOffsets[0]);
  collision.reason = SyncSkipReason::CaseCollision;
  SyncOp unsafe = makeOp(SyncOpKind::Skip, NO_OFFSET, treeOffsets[1]);
  unsafe.reason = SyncSkipReason::UnsafeName;
  EXPECT_EQ(executor->execute(collision), SyncOpOutcome::Skipped);
  EXPECT_EQ(executor->execute(unsafe), SyncOpOutcome::Skipped);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.skippedCaseCollision, 1u);
  EXPECT_EQ(result.skippedUnsafeName, 1u);
  EXPECT_EQ(result.fileErrors, 0u);
  EXPECT_EQ(storage.mutatingCalls(), 0u);
}

TEST_F(SyncExecutorTest, SinkReportsProgressWithThePlannerTotalBeforeEachOperation) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"old/z.epub", "Z"}}, &manifestOffsets);
  auto executor = begin({{"Livres/a.epub", "A"}, {"b.epub", "B"}});
  const SyncOpSink sink = executor->sink();

  sink.onTotal(sink.ctx, 3);
  EXPECT_TRUE(sink.onOp(sink.ctx, makeOp(SyncOpKind::AdoptOrDownload, NO_OFFSET, treeOffsets[0])));
  EXPECT_TRUE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[1])));
  EXPECT_TRUE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Delete, manifestOffsets[0], NO_OFFSET)));
  executor->end();

  const std::vector<Progress> expected = {
      {1, 3, SyncOpKind::AdoptOrDownload, "Livres/a.epub"},
      {2, 3, SyncOpKind::Download, "b.epub"},
      {3, 3, SyncOpKind::Delete, "old/z.epub"},
  };
  EXPECT_EQ(progress, expected);
}

TEST_F(SyncExecutorTest, PerFileErrorKeepsTheSinkGoingAndAFatalOneStopsIt) {
  auto executor = begin({{"a.epub", "A"}, {"b.epub", "B"}});
  const SyncOpSink sink = executor->sink();
  sink.onTotal(sink.ctx, 2);

  fetchMode = FetchMode::WrongBytes;
  EXPECT_TRUE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])));
  clearResult = false;
  fetchMode = FetchMode::Serve;
  EXPECT_FALSE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[1])));
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::StorageError);
  EXPECT_EQ(result.fileErrors, 1u);
}

TEST_F(SyncExecutorTest, CancelStopsBeforeTheNextOperationWithAConsistentManifest) {
  auto executor = begin({{"a.epub", "A"}, {"b.epub", "B"}, {"c.epub", "C"}});
  const SyncOpSink sink = executor->sink();
  sink.onTotal(sink.ctx, 3);

  EXPECT_TRUE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])));
  EXPECT_TRUE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[1])));
  executor->requestCancel();
  storage.resetCounters();
  EXPECT_FALSE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[2])));
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Cancelled);
  EXPECT_EQ(result.added, 2u);
  EXPECT_EQ(progress.size(), 2u);
  EXPECT_EQ(storage.mutatingCalls(), 0u);
  EXPECT_FALSE(storage.exists("/c.epub"));
  const auto live = liveEntries(storage);
  EXPECT_EQ(live.size(), 2u);
  EXPECT_EQ(live.count("c.epub"), 0u);
}

TEST_F(SyncExecutorTest, RefusedOperationStillCountsInProgress) {
  auto executor = begin({{".crosspoint/evil.bin", "evil"}, {"a.epub", "A"}});
  const SyncOpSink sink = executor->sink();
  sink.onTotal(sink.ctx, 2);

  EXPECT_TRUE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])));
  EXPECT_TRUE(sink.onOp(sink.ctx, makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[1])));
  executor->end();

  ASSERT_EQ(progress.size(), 2u);
  EXPECT_EQ(progress[1], (Progress{2, 2, SyncOpKind::Download, "a.epub"}));
}

TEST_F(SyncExecutorTest, NameTheCardRefusesIsAPerFileError) {
  auto executor = begin({{"b:c.epub", "B"}});

  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])),
            SyncOpOutcome::TargetUnwritable);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.fileErrors, 1u);
  EXPECT_FALSE(storage.exists(SYNC_DOWNLOAD_TMP_PATH));
  EXPECT_TRUE(liveEntries(storage).empty());
}

TEST_F(SyncExecutorTest, JournalThatCannotBeCompactedIsAManifestErrorAndKeepsTheManifest) {
  std::vector<uint32_t> offsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "A"}}, &offsets);
  {
    SyncFile* file = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Append);
    ASSERT_NE(file, nullptr);
    auto writer = std::make_unique<RecordWriter>(*file);
    ASSERT_TRUE(writer->appendTo(static_cast<uint32_t>(file->size())));
    ASSERT_TRUE(writer->drop(offsets[0]));
    ASSERT_TRUE(writer->drop(offsets[0]));
    storage.close(file);
  }
  const std::string before = fileContent(storage, SYNC_MANIFEST_PATH);

  const SyncRunResult result = makeExecutor()->prepare();

  EXPECT_EQ(result.status, SyncRunStatus::ManifestError);
  EXPECT_EQ(result.recordStatus, RecordStatus::Corrupt);
  EXPECT_EQ(fileContent(storage, SYNC_MANIFEST_PATH), before);
  EXPECT_FALSE(storage.exists(SYNC_MANIFEST_NEW_PATH));
}

TEST_F(SyncExecutorTest, CompactionInterruptedAtAnyCallIsFinishedByTheNextPrepare) {
  std::vector<uint32_t> offsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "A"}, {"b.epub", "B"}}, &offsets);
  {
    SyncFile* file = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Append);
    ASSERT_NE(file, nullptr);
    auto writer = std::make_unique<RecordWriter>(*file);
    ASSERT_TRUE(writer->appendTo(static_cast<uint32_t>(file->size())));
    ASSERT_TRUE(writer->drop(offsets[0]));
    storage.close(file);
  }
  const MemorySyncStorage::Snapshot initial = storage.snapshot();
  const auto expected = liveEntries(storage);
  storage.resetCounters();
  ASSERT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok);
  const size_t calls = storage.mutatingCalls();
  const auto compacted = storage.snapshot();
  ASSERT_GT(calls, 0u);

  for (size_t n = 1; n <= calls; ++n) {
    for (const auto tear : {MemorySyncStorage::Tear::None, MemorySyncStorage::Tear::Half}) {
      MemorySyncStorage card;
      for (const auto& [path, content] : initial.files) seedFile(card, path, content);
      storage = std::move(card);
      storage.resetCounters();
      storage.failAt(n, tear);
      EXPECT_NE(makeExecutor()->prepare().status, SyncRunStatus::Ok) << "n=" << n;
      storage.clearFault();

      EXPECT_EQ(makeExecutor()->prepare().status, SyncRunStatus::Ok) << "n=" << n;
      EXPECT_EQ(storage.snapshot(), compacted) << "n=" << n;
      EXPECT_EQ(liveEntries(storage), expected);
    }
  }
}

// One operation interrupted at its n-th mutating call (fatal, or a per-file error for a refused mkdirs or rename)
// never reaches its journal frame, so the manifest keeps its offsets and the same operation runs again; it must
// end exactly like an uninterrupted run.
TEST_F(SyncExecutorTest, OperationInterruptedAtAnyCallEndsTheSameWhenRunAgain) {
  struct Scenario {
    const char* name;
    std::vector<Entry> manifest;
    std::vector<Entry> localFiles;
    std::vector<Entry> tree;
    SyncOpKind kind;
    bool usesManifestEntry;
  };
  const std::vector<Scenario> scenarios = {
      {"modify",
       {{"Livres/a.epub", "old"}},
       {{"/Livres/a.epub", "old"}},
       {{"Livres/a.epub", "new content"}},
       SyncOpKind::Download,
       true},
      {"new nested", {}, {}, {{"A/B/c.epub", "nested"}}, SyncOpKind::Download, false},
      {"adopt mismatch", {}, {{"/d.epub", "local"}}, {{"d.epub", "remote"}}, SyncOpKind::AdoptOrDownload, false},
      {"delete", {{"A/B/x.epub", "X"}}, {{"/A/B/x.epub", "X"}, {"/A/mine.txt", "user"}}, {}, SyncOpKind::Delete, true},
      {"move",
       {{"Livres/SF/Dune.epub", "spice"}},
       {{"/Livres/SF/Dune.epub", "spice"},
        {cacheDir(BookCacheKind::Epub, "/Livres/SF/Dune.epub") + "/section.bin", "mine"},
        {cacheDir(BookCacheKind::Txt, "/Livres/SF/Dune.epub") + "/section.bin", "mine"},
        {bookmarkPath("/Livres/SF/Dune.epub"), "mine"},
        {cacheDir(BookCacheKind::Epub, "/Lus/Dune.epub") + "/section.bin", "stale"}},
       {{"Lus/Dune.epub", "spice"}},
       SyncOpKind::Move,
       true},
  };
  for (const Scenario& scenario : scenarios) {
    auto seed = [&](MemorySyncStorage& card) {
      std::vector<uint32_t> manifestOffsets;
      writeRecords(card, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, scenario.manifest, &manifestOffsets);
      for (const Entry& file : scenario.localFiles) seedFile(card, file.path, file.content);
      return manifestOffsets;
    };
    auto runOnce = [&](const std::vector<uint32_t>& manifestOffsets) {
      auto executor = begin(scenario.tree);
      const uint32_t manifestOffset = scenario.usesManifestEntry ? manifestOffsets[0] : NO_OFFSET;
      const uint32_t treeOffset = treeOffsets.empty() ? NO_OFFSET : treeOffsets[0];
      executor->execute(makeOp(scenario.kind, manifestOffset, treeOffset));
      return executor->end().status;
    };

    storage = MemorySyncStorage();
    std::vector<uint32_t> offsets = seed(storage);
    // The tree is written by begin(); count only the executor's own calls.
    auto executor = begin(scenario.tree);
    storage.resetCounters();
    const uint32_t treeOffset = treeOffsets.empty() ? NO_OFFSET : treeOffsets[0];
    ASSERT_NE(executor->execute(makeOp(scenario.kind, scenario.usesManifestEntry ? offsets[0] : NO_OFFSET, treeOffset)),
              SyncOpOutcome::Fatal);
    ASSERT_EQ(executor->end().status, SyncRunStatus::Ok) << scenario.name;
    executor.reset();
    const size_t calls = storage.mutatingCalls();
    const auto expectedLive = liveEntries(storage);
    auto expectedFiles = storage.snapshot();
    expectedFiles.files.erase(SYNC_MANIFEST_PATH);

    for (size_t n = 1; n <= calls; ++n) {
      for (const auto tear : {MemorySyncStorage::Tear::None, MemorySyncStorage::Tear::Half}) {
        storage = MemorySyncStorage();
        offsets = seed(storage);
        auto interrupted = begin(scenario.tree);
        storage.resetCounters();
        storage.failAt(n, tear);
        interrupted->execute(makeOp(scenario.kind, scenario.usesManifestEntry ? offsets[0] : NO_OFFSET,
                                    treeOffsets.empty() ? NO_OFFSET : treeOffsets[0]));
        interrupted->end();
        interrupted.reset();
        storage.clearFault();

        EXPECT_EQ(runOnce(offsets), SyncRunStatus::Ok) << scenario.name << " n=" << n;
        auto files = storage.snapshot();
        files.files.erase(SYNC_MANIFEST_PATH);
        EXPECT_EQ(files, expectedFiles) << scenario.name << " n=" << n;
        EXPECT_EQ(liveEntries(storage), expectedLive) << scenario.name << " n=" << n;
      }
    }
  }
}

TEST_F(SyncExecutorTest, SecondRunOnTheSameExecutorContinuesTheJournal) {
  auto executor = begin({{"a.epub", "A"}, {"b.epub", "B"}});
  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[0])), SyncOpOutcome::Applied);
  executor->end();

  ASSERT_TRUE(executor->begin());
  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, NO_OFFSET, treeOffsets[1])), SyncOpOutcome::Applied);
  EXPECT_EQ(executor->end().status, SyncRunStatus::Ok);

  EXPECT_EQ(liveEntries(storage).size(), 2u);
}

TEST_F(SyncExecutorTest, MoveRenamesTheFileWithoutAFetchAndJournalsTheDestination) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/SF/Dune.epub", "spice"}},
               &manifestOffsets);
  seedFile(storage, "/Livres/SF/Dune.epub", "spice");
  auto executor = begin({{"Lus/Dune.epub", "spice"}});

  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.moved, 1u);
  EXPECT_EQ(fileContent(storage, "/Lus/Dune.epub"), "spice");
  EXPECT_EQ(storage.snapshot().files.count("/Livres/SF/Dune.epub"), 0u);
  EXPECT_TRUE(fetches.empty());
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.at("Lus/Dune.epub"), (LiveEntry{hex(blobSha("spice").data()), 5}));
}

TEST_F(SyncExecutorTest, MovedBookKeepsItsCachesAndBookmarksUnderTheReaderDerivedPaths) {
  for (const auto& [from, to] : {std::pair<std::string, std::string>{"Livres/SF/Dune.epub", "Lus/Dune.epub"},
                                 std::pair<std::string, std::string>{"Livres/notes.txt", "Livres/Lu/notes.txt"}}) {
    storage = MemorySyncStorage();
    const std::string source = "/" + from;
    const std::string target = "/" + to;
    std::vector<uint32_t> manifestOffsets;
    writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{from, "book"}}, &manifestOffsets);
    seedFile(storage, source, "book");
    seedReadingState(storage, source);
    auto executor = begin({{to, "book"}});

    EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Applied);
    const SyncRunResult result = executor->end();

    EXPECT_EQ(result.stateConflicts, 0u);
    const auto files = storage.snapshot().files;
    for (const BookCacheKind kind : CACHE_KINDS) {
      EXPECT_EQ(fileContent(storage, cacheDir(kind, target) + "/section.bin"), source) << to;
      EXPECT_FALSE(storage.exists(cacheDir(kind, source).c_str())) << to;
    }
    EXPECT_EQ(fileContent(storage, bookmarkPath(target)), source) << to;
    EXPECT_EQ(files.count(bookmarkPath(source)), 0u) << to;
  }
}

TEST_F(SyncExecutorTest, MovedBookFollowsInRecentBooksAndAsTheOpenBook) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/a.epub", "A"}}, &manifestOffsets);
  seedFile(storage, "/Livres/a.epub", "A");
  recentBooks = {"/other.epub", "/Livres/a.epub"};
  openBook = "/Livres/a.epub";
  auto executor = begin({{"Lus/a.epub", "A"}});

  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Applied);
  executor->end();

  EXPECT_EQ(moves, (std::vector<std::pair<std::string, std::string>>{{"/Livres/a.epub", "/Lus/a.epub"}}));
  EXPECT_EQ(recentBooks, (std::vector<std::string>{"/other.epub", "/Lus/a.epub"}));
  EXPECT_EQ(openBook, "/Lus/a.epub");
}

TEST_F(SyncExecutorTest, MovePrunesTheSourceFoldersItEmptiedAndKeepsOnesWithUserFiles) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest,
               {{"Livres/SF/Dune.epub", "D"}, {"Mes/b.epub", "B"}, {"Mes/c.epub", "C"}}, &manifestOffsets);
  seedFile(storage, "/Livres/SF/Dune.epub", "D");
  seedFile(storage, "/Mes/b.epub", "B");
  seedFile(storage, "/Mes/mine.txt", "user file");
  seedFile(storage, "/Mes/c.epub", "C");
  auto executor = begin({{"Lus/Dune.epub", "D"}, {"Lus/b.epub", "B"}, {"Mes/renamed.epub", "C"}});

  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(executor->execute(moveOp(manifestOffsets[i], treeOffsets[i], true, false)), SyncOpOutcome::Applied);
  }
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.moved, 3u);
  EXPECT_EQ(result.pruned, 2u);
  const auto snap = storage.snapshot();
  EXPECT_EQ(snap.directories.count("/Livres/SF"), 0u);
  EXPECT_EQ(snap.directories.count("/Livres"), 0u);
  EXPECT_EQ(fileContent(storage, "/Mes/mine.txt"), "user file");
  EXPECT_EQ(fileContent(storage, "/Mes/renamed.epub"), "C");
}

TEST_F(SyncExecutorTest, StaleCacheAtTheDestinationIsClearedThroughTheHookBeforeTheRename) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/Dune.epub", "D"}}, &manifestOffsets);
  seedFile(storage, "/Livres/Dune.epub", "D");
  seedFile(storage, cacheDir(BookCacheKind::Epub, "/Livres/Dune.epub") + "/section.bin", "mine");
  seedFile(storage, cacheDir(BookCacheKind::Epub, "/Lus/Dune.epub") + "/section.bin", "stale");
  auto executor = begin({{"Lus/Dune.epub", "D"}});

  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.stateConflicts, 0u);
  EXPECT_EQ(cleared, std::vector<std::string>{"/Lus/Dune.epub"});
  EXPECT_EQ(fileContent(storage, cacheDir(BookCacheKind::Epub, "/Lus/Dune.epub") + "/section.bin"), "mine");
  EXPECT_FALSE(storage.exists(cacheDir(BookCacheKind::Epub, "/Livres/Dune.epub").c_str()));
}

TEST_F(SyncExecutorTest, ReadingStateTheDestinationAlreadyHoldsIsLeftAndCountedAsAConflict) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/Dune.epub", "D"}}, &manifestOffsets);
  seedFile(storage, "/Livres/Dune.epub", "D");
  seedReadingState(storage, "/Livres/Dune.epub");
  // The hook clears an .epub's epub_ cache only; the txt_ cache and the bookmark stay.
  seedFile(storage, cacheDir(BookCacheKind::Txt, "/Lus/Dune.epub") + "/section.bin", "theirs");
  seedFile(storage, bookmarkPath("/Lus/Dune.epub"), "theirs");
  auto executor = begin({{"Lus/Dune.epub", "D"}});

  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.moved, 1u);
  EXPECT_EQ(result.stateConflicts, 2u);
  EXPECT_EQ(fileContent(storage, cacheDir(BookCacheKind::Txt, "/Lus/Dune.epub") + "/section.bin"), "theirs");
  EXPECT_EQ(fileContent(storage, cacheDir(BookCacheKind::Txt, "/Livres/Dune.epub") + "/section.bin"),
            "/Livres/Dune.epub");
  EXPECT_EQ(fileContent(storage, bookmarkPath("/Lus/Dune.epub")), "theirs");
  EXPECT_EQ(fileContent(storage, bookmarkPath("/Livres/Dune.epub")), "/Livres/Dune.epub");
  EXPECT_EQ(fileContent(storage, cacheDir(BookCacheKind::Epub, "/Lus/Dune.epub") + "/section.bin"),
            "/Livres/Dune.epub");
  EXPECT_EQ(liveEntries(storage).count("Lus/Dune.epub"), 1u);
}

TEST_F(SyncExecutorTest, MoveBetweenNamesSharingABookmarkLeavesTheBookmarkAlone) {
  for (const char* to : {"a_b.epub", "A_b.epub"}) {
    storage = MemorySyncStorage();
    std::vector<uint32_t> manifestOffsets;
    writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a/b.epub", "B"}}, &manifestOffsets);
    seedFile(storage, "/a/b.epub", "B");
    seedFile(storage, bookmarkPath("/a/b.epub"), "mine");
    auto executor = begin({{to, "B"}});

    EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Applied);
    const SyncRunResult result = executor->end();

    EXPECT_EQ(result.stateConflicts, 0u) << to;
    EXPECT_EQ(fileContent(storage, bookmarkPath("/a/b.epub")), "mine") << to;
    EXPECT_EQ(liveEntries(storage).count(to), 1u) << to;
  }
}

TEST_F(SyncExecutorTest, MoveWhoseFileAlreadyMovedIsVerifiedThenMigratesTheReadingState) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/Dune.epub", "spice"}},
               &manifestOffsets);
  seedFile(storage, "/Lus/Dune.epub", "spice");
  seedReadingState(storage, "/Livres/Dune.epub");
  auto executor = begin({{"Lus/Dune.epub", "spice"}});

  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], false, true)), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.moved, 1u);
  EXPECT_EQ(hashed, std::vector<std::string>{"/Lus/Dune.epub"});
  EXPECT_TRUE(fetches.empty());
  for (const BookCacheKind kind : CACHE_KINDS) {
    EXPECT_TRUE(storage.exists(cacheDir(kind, "/Lus/Dune.epub").c_str()));
  }
  EXPECT_EQ(fileContent(storage, bookmarkPath("/Lus/Dune.epub")), "/Livres/Dune.epub");
  EXPECT_EQ(moves, (std::vector<std::pair<std::string, std::string>>{{"/Livres/Dune.epub", "/Lus/Dune.epub"}}));
  EXPECT_EQ(liveEntries(storage).count("Lus/Dune.epub"), 1u);
}

TEST_F(SyncExecutorTest, MoveWhoseDestinationDiffersDownloadsItThenMigratesTheReadingState) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/Dune.epub", "spice"}},
               &manifestOffsets);
  seedFile(storage, "/Lus/Dune.epub", "SPICE");
  seedReadingState(storage, "/Livres/Dune.epub");
  auto executor = begin({{"Lus/Dune.epub", "spice"}});

  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], false, true)), SyncOpOutcome::Applied);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.moved, 1u);
  EXPECT_EQ(result.added, 0u);
  EXPECT_EQ(fileContent(storage, "/Lus/Dune.epub"), "spice");
  EXPECT_EQ(fetches[hex(blobSha("spice").data())], 1);
  EXPECT_EQ(fileContent(storage, cacheDir(BookCacheKind::Epub, "/Lus/Dune.epub") + "/section.bin"),
            "/Livres/Dune.epub");
  EXPECT_EQ(fileContent(storage, bookmarkPath("/Lus/Dune.epub")), "/Livres/Dune.epub");
  EXPECT_EQ(moves.size(), 1u);
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.at("Lus/Dune.epub").shaHex, hex(blobSha("spice").data()));
}

TEST_F(SyncExecutorTest, MoveThatCannotRenameIsAPerFileErrorWithNothingMigrated) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/a.epub", "A"}}, &manifestOffsets);
  seedFile(storage, "/Livres/a.epub", "A");
  seedReadingState(storage, "/Livres/a.epub");
  seedFile(storage, "/Lus", "a file where a folder should be");
  auto executor = begin({{"Lus/a.epub", "A"}});
  const auto before = storage.snapshot();

  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)),
            SyncOpOutcome::TargetUnwritable);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.fileErrors, 1u);
  EXPECT_EQ(result.moved, 0u);
  auto after = storage.snapshot();
  after.files[SYNC_MANIFEST_PATH] = before.files.at(SYNC_MANIFEST_PATH);
  EXPECT_EQ(after, before);
  EXPECT_TRUE(moves.empty());
  EXPECT_EQ(liveEntries(storage).count("Livres/a.epub"), 1u);
}

TEST_F(SyncExecutorTest, FailingMoveHookIsFatalAndTheMoveIsNotJournaled) {
  std::vector<uint32_t> manifestOffsets;
  writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/a.epub", "A"}}, &manifestOffsets);
  seedFile(storage, "/Livres/a.epub", "A");
  auto executor = begin({{"Lus/a.epub", "A"}});
  movedResult = false;

  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Fatal);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::StorageError);
  EXPECT_EQ(result.moved, 0u);
  EXPECT_EQ(liveEntries(storage).count("Livres/a.epub"), 1u);
  EXPECT_TRUE(storage.exists("/Livres")) << "the source folder is pruned only after the hook";
}

TEST_F(SyncExecutorTest, MoveInterruptedRightAfterItsFileRenameMigratesTheStateOnTheNextRun) {
  std::vector<uint32_t> manifestOffsets;
  const auto seed = [&] {
    storage = MemorySyncStorage();
    manifestOffsets.clear();
    writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"Livres/SF/Dune.epub", "spice"}},
                 &manifestOffsets);
    seedFile(storage, "/Livres/SF/Dune.epub", "spice");
    seedReadingState(storage, "/Livres/SF/Dune.epub");
  };
  seed();
  auto executor = begin({{"Lus/Dune.epub", "spice"}});
  storage.resetCounters();
  ASSERT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Applied);
  executor->end();
  const auto& log = storage.calls();
  const auto fileRename = std::find(
      log.begin(), log.end(),
      MemorySyncStorage::Call{MemorySyncStorage::CallKind::Rename, "/Livres/SF/Dune.epub", "/Lus/Dune.epub", true});
  ASSERT_NE(fileRename, log.end());
  const size_t failAt = static_cast<size_t>(fileRename - log.begin()) + 2;
  moves.clear();

  seed();
  executor = begin({{"Lus/Dune.epub", "spice"}});
  storage.resetCounters();
  storage.failAt(failAt, MemorySyncStorage::Tear::None);
  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], true, false)), SyncOpOutcome::Fatal);
  executor->end();
  storage.clearFault();

  EXPECT_EQ(fileContent(storage, "/Lus/Dune.epub"), "spice");
  EXPECT_TRUE(storage.exists(cacheDir(BookCacheKind::Epub, "/Livres/SF/Dune.epub").c_str()));
  EXPECT_TRUE(moves.empty());

  executor = begin({{"Lus/Dune.epub", "spice"}});
  EXPECT_EQ(executor->execute(moveOp(manifestOffsets[0], treeOffsets[0], false, true)), SyncOpOutcome::Applied);
  EXPECT_EQ(executor->end().status, SyncRunStatus::Ok);

  for (const BookCacheKind kind : CACHE_KINDS) {
    EXPECT_EQ(fileContent(storage, cacheDir(kind, "/Lus/Dune.epub") + "/section.bin"), "/Livres/SF/Dune.epub");
    EXPECT_FALSE(storage.exists(cacheDir(kind, "/Livres/SF/Dune.epub").c_str()));
  }
  EXPECT_EQ(fileContent(storage, bookmarkPath("/Lus/Dune.epub")), "/Livres/SF/Dune.epub");
  EXPECT_FALSE(storage.exists("/Livres"));
  EXPECT_EQ(moves, (std::vector<std::pair<std::string, std::string>>{{"/Livres/SF/Dune.epub", "/Lus/Dune.epub"}}));
  EXPECT_TRUE(fetches.empty());
  const auto live = liveEntries(storage);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live.count("Lus/Dune.epub"), 1u);
}

TEST_F(SyncExecutorTest, ModifyInterruptedRightAfterItsTargetRenameClearsTheCacheOnTheNextRun) {
  std::vector<uint32_t> manifestOffsets;
  const auto seed = [&] {
    storage = MemorySyncStorage();
    manifestOffsets.clear();
    writeRecords(storage, SYNC_MANIFEST_PATH, RecordFileKind::Manifest, {{"a.epub", "old"}}, &manifestOffsets);
    seedFile(storage, "/a.epub", "old");
  };
  seed();
  auto executor = begin({{"a.epub", "brand new"}});
  storage.resetCounters();
  ASSERT_EQ(executor->execute(makeOp(SyncOpKind::Download, manifestOffsets[0], treeOffsets[0])),
            SyncOpOutcome::Applied);
  executor->end();
  const auto& log = storage.calls();
  const auto targetRename =
      std::find(log.begin(), log.end(),
                MemorySyncStorage::Call{MemorySyncStorage::CallKind::Rename, SYNC_DOWNLOAD_TMP_PATH, "/a.epub", true});
  ASSERT_NE(targetRename, log.end());
  const size_t failAt = static_cast<size_t>(targetRename - log.begin()) + 2;

  seed();
  executor = begin({{"a.epub", "brand new"}});
  storage.resetCounters();
  storage.failAt(failAt, MemorySyncStorage::Tear::None);
  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, manifestOffsets[0], treeOffsets[0])), SyncOpOutcome::Fatal);
  executor->end();
  storage.clearFault();
  EXPECT_EQ(fileContent(storage, "/a.epub"), "brand new");
  // The reader opened the new file through the old cache before the next run.
  seedFile(storage, cacheDir(BookCacheKind::Epub, "/a.epub") + "/section.bin", "old layout");
  cleared.clear();

  executor = begin({{"a.epub", "brand new"}});
  EXPECT_EQ(executor->execute(makeOp(SyncOpKind::Download, manifestOffsets[0], treeOffsets[0])),
            SyncOpOutcome::Applied);
  EXPECT_EQ(executor->end().status, SyncRunStatus::Ok);

  EXPECT_EQ(cleared, std::vector<std::string>{"/a.epub"});
  EXPECT_FALSE(storage.exists(cacheDir(BookCacheKind::Epub, "/a.epub").c_str()));
  EXPECT_EQ(fileContent(storage, "/a.epub"), "brand new");
  EXPECT_EQ(liveEntries(storage).at("a.epub").shaHex, hex(blobSha("brand new").data()));
}
