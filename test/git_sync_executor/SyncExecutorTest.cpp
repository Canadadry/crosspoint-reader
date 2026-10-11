#include <GitBlobHasher.h>
#include <Memory.h>
#include <RecordReader.h>
#include <RecordWriter.h>
#include <SyncExecutor.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "MemorySyncStorage.h"

namespace {

using Sha = std::array<uint8_t, GitBlobHasher::SHA_SIZE>;

Sha blobSha(const std::string& content) {
  GitBlobHasher hasher;
  hasher.begin(content.size());
  hasher.update(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  Sha sha{};
  hasher.finish(sha.data());
  return sha;
}

std::string hex(const uint8_t* sha) {
  char out[SHA_HEX_LENGTH + 1];
  shaToHex(sha, out);
  return out;
}

struct Entry {
  std::string path;
  std::string content;
};

struct LiveEntry {
  std::string shaHex;
  uint64_t size;
  bool operator==(const LiveEntry& other) const { return shaHex == other.shaHex && size == other.size; }
};

void seedFile(MemorySyncStorage& storage, const std::string& path, const std::string& content) {
  const size_t slash = path.rfind('/');
  if (slash > 0) {
    ASSERT_TRUE(storage.mkdirs(path.substr(0, slash).c_str()));
  }
  SyncFile* file = storage.open(path.c_str(), SyncOpenMode::Truncate);
  ASSERT_NE(file, nullptr);
  ASSERT_TRUE(file->write(reinterpret_cast<const uint8_t*>(content.data()), content.size()));
  storage.close(file);
}

std::string fileContent(MemorySyncStorage& storage, const std::string& path) {
  const auto snap = storage.snapshot();
  const auto it = snap.files.find(path);
  return it == snap.files.end() ? std::string("<missing>") : it->second;
}

// Writes a record file of the given entries; offsets receives each entry's frame offset.
void writeRecords(MemorySyncStorage& storage, const char* path, RecordFileKind kind, const std::vector<Entry>& entries,
                  std::vector<uint32_t>* offsets) {
  ASSERT_TRUE(storage.mkdirs(SYNC_WORK_DIR));
  SyncFile* file = storage.open(path, SyncOpenMode::Truncate);
  ASSERT_NE(file, nullptr);
  auto writer = std::make_unique<RecordWriter>(*file);
  ASSERT_TRUE(writer->begin(kind));
  for (const Entry& entry : entries) {
    const Sha sha = blobSha(entry.content);
    uint32_t offset = 0;
    ASSERT_TRUE(
        writer->put(sha.data(), entry.content.size(), entry.path.data(), entry.path.size(), NO_OFFSET, &offset));
    if (offsets) offsets->push_back(offset);
  }
  ASSERT_TRUE(writer->seal());
  storage.close(file);
}

// Live manifest entries by repository path, journal applied.
std::map<std::string, LiveEntry> liveEntries(MemorySyncStorage& storage) {
  std::map<std::string, LiveEntry> live;
  SyncFile* file = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Read);
  EXPECT_NE(file, nullptr);
  if (!file) return live;
  auto reader = std::make_unique<RecordReader>(*file, RecordFileKind::Manifest);
  std::map<uint32_t, std::string> byOffset;
  SyncRecord record;
  RecordStatus status;
  while ((status = reader->next(record)) == RecordStatus::Record) {
    const uint32_t target = record.kind == RecordFrameKind::Drop ? record.target : record.replaces;
    if (target != NO_OFFSET) {
      live.erase(byOffset[target]);
    }
    if (record.kind == RecordFrameKind::Entry) {
      byOffset[record.offset] = record.path;
      live[record.path] = {hex(record.sha), record.size};
    }
  }
  EXPECT_EQ(status, RecordStatus::End);
  storage.close(file);
  return live;
}

size_t journalFrames(MemorySyncStorage& storage) {
  SyncFile* file = storage.open(SYNC_MANIFEST_PATH, SyncOpenMode::Read);
  EXPECT_NE(file, nullptr);
  if (!file) return 0;
  auto reader = std::make_unique<RecordReader>(*file, RecordFileKind::Manifest);
  SyncRecord record;
  size_t count = 0;
  while (reader->next(record) == RecordStatus::Record) {
    if (record.inJournal) ++count;
  }
  storage.close(file);
  return count;
}

SyncOp makeOp(SyncOpKind kind, uint32_t manifestOffset, uint32_t treeOffset) {
  return {kind, SyncSkipReason::None, false, false, manifestOffset, treeOffset};
}

class SyncExecutorTest : public ::testing::Test {
 protected:
  enum class FetchMode : uint8_t { Serve, WrongBytes, ShortBody, ExtraBytes, FailMidStream };

  SyncExecutorCallbacks callbacks() { return {this, &fetchFake, &hashFake, &clearFake, nullptr, &progressFake}; }

  std::unique_ptr<SyncExecutor> makeExecutor() { return makeUniqueNoThrow<SyncExecutor>(storage, callbacks()); }

  // Serves content by SHA in 3-byte chunks, as the downloader streams it.
  void serve(const std::string& content) { blobs[hex(blobSha(content).data())] = content; }

  static bool fetchFake(void* ctx, const SyncFetchRequest& request, const SyncChunkSink& sink) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    const std::string key = hex(request.sha);
    self->fetches[key]++;
    const auto it = self->blobs.find(key);
    if (it == self->blobs.end()) return false;
    std::string body = it->second;
    switch (self->fetchMode) {
      case FetchMode::Serve:
      case FetchMode::FailMidStream:
        break;
      case FetchMode::WrongBytes:
        body[0] ^= 1;
        break;
      case FetchMode::ShortBody:
        body.pop_back();
        break;
      case FetchMode::ExtraBytes:
        body += "!";
        break;
    }
    const size_t stopAt = self->fetchMode == FetchMode::FailMidStream ? body.size() / 2 : body.size();
    for (size_t pos = 0; pos < stopAt; pos += 3) {
      const size_t len = std::min<size_t>(3, stopAt - pos);
      if (!sink.write(sink.ctx, reinterpret_cast<const uint8_t*>(body.data()) + pos, len)) return false;
    }
    return self->fetchMode != FetchMode::FailMidStream;
  }

  // Reads the card the way the device glue does, into a blob hasher.
  static bool hashFake(void* ctx, const char* absPath, uint64_t expectedSize, uint8_t outSha[GitBlobHasher::SHA_SIZE]) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    self->hashed.push_back(absPath);
    if (absPath == self->unhashable) return false;
    SyncFile* file = self->storage.open(absPath, SyncOpenMode::Read);
    if (!file) return false;
    GitBlobHasher hasher;
    hasher.begin(expectedSize);
    uint8_t buf[5];
    int n;
    while ((n = file->read(buf, sizeof(buf))) > 0) hasher.update(buf, static_cast<size_t>(n));
    self->storage.close(file);
    return hasher.finish(outSha);
  }

  struct Progress {
    uint32_t done;
    uint32_t total;
    SyncOpKind kind;
    std::string repoPath;
    bool operator==(const Progress& o) const {
      return done == o.done && total == o.total && kind == o.kind && repoPath == o.repoPath;
    }
  };

  static void progressFake(void* ctx, const SyncProgress& progress) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    self->progress.push_back({progress.done, progress.total, progress.kind, progress.repoPath});
  }

  static bool clearFake(void* ctx, const char* absPath) {
    auto* self = static_cast<SyncExecutorTest*>(ctx);
    self->cleared.push_back(absPath);
    return self->clearResult;
  }

  // Prepares an empty card's manifest, or the one already seeded, then writes the tree and begins.
  std::unique_ptr<SyncExecutor> begin(const std::vector<Entry>& tree) {
    auto executor = makeExecutor();
    EXPECT_EQ(executor->prepare().status, SyncRunStatus::Ok);
    treeOffsets.clear();
    writeRecords(storage, SYNC_TREE_PATH, RecordFileKind::Tree, tree, &treeOffsets);
    for (const Entry& entry : tree) serve(entry.content);
    EXPECT_TRUE(executor->begin());
    return executor;
  }

  MemorySyncStorage storage;
  std::map<std::string, std::string> blobs;
  std::map<std::string, int> fetches;
  FetchMode fetchMode = FetchMode::Serve;
  std::vector<std::string> cleared;
  bool clearResult = true;
  std::vector<std::string> hashed;
  // The hash callback answers "unknown" for this path.
  std::string unhashable;
  std::vector<uint32_t> treeOffsets;
  std::vector<Progress> progress;
};

}  // namespace

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
  };
  for (const SyncOp& op : ops) EXPECT_EQ(executor->execute(op), SyncOpOutcome::RefusedPath);
  const SyncRunResult result = executor->end();

  EXPECT_EQ(result.status, SyncRunStatus::Ok);
  EXPECT_EQ(result.fileErrors, 6u);
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
