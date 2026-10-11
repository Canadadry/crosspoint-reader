#include <GitSyncRun.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "Logging.h"
#include "SyncExecutorFixture.h"
#include "SyncPlanner.h"
#include "TreeFixture.h"
#include "TreeSnapshotWriter.h"

// makeUniqueNoThrow goes through the nothrow forms; an allocation of failNothrowSize bytes fails.
namespace {
size_t failNothrowSize = 0;
}  // namespace

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (failNothrowSize != 0 && size == failNothrowSize) return nullptr;
  return ::operator new(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (failNothrowSize != 0 && size == failNothrowSize) return nullptr;
  return ::operator new[](size);
}

namespace {

// The fixture's syncable files, by absolute path.
std::map<std::string, std::string> fixtureFiles() {
  std::map<std::string, std::string> files;
  for (const auto& blob : TREE_FIXTURE_BLOBS) {
    if (blob.path[0] != '.') files[std::string("/") + blob.path] = blob.content;
  }
  return files;
}

// Card files outside the work and state directory.
std::map<std::string, std::string> bookFiles(MemorySyncStorage& storage) {
  std::map<std::string, std::string> files;
  for (const auto& [path, content] : storage.snapshot().files) {
    if (path.compare(0, 12, "/.crosspoint") != 0) files[path] = content;
  }
  return files;
}

// The fixture body without README.md, so planning it on a synced card would delete a file.
std::string treeWithoutReadme() {
  std::string body = TREE_FIXTURE_BODY;
  const size_t from = body.find(R"({"path":"README.md")");
  const size_t to = body.find(R"({"path":"vendor")");
  body.erase(from, to - from);
  return body;
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
  const size_t at = text.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  if (at != std::string::npos) text.replace(at, from.size(), to);
  return text;
}

// A trees body of count syncable blobs, all with the empty file's SHA.
std::string generatedTree(size_t count) {
  std::string body = R"({"sha":"859af781495f25be7e50e1e4c78274d53de4c5af","tree":[)";
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) body += ",";
    body += R"({"path":"f)" + std::to_string(i) +
            R"(.txt","mode":"100644","type":"blob","sha":"e69de29bb2d1d6434b8b29ae775ad8c2e48c5391","size":0})";
  }
  return body + R"(],"truncated":false})";
}

}  // namespace

void PrintTo(GitSyncError error, std::ostream* os) { *os << describeGitSyncError(error); }

// Keeps gtest from printing byte dumps of the containers.
void PrintTo(const std::map<std::string, std::string>& map, std::ostream* os) {
  for (const auto& [key, value] : map) *os << "\n  " << key << " = " << value;
}

namespace {

class GitSyncRunTest : public ::testing::Test {
 protected:
  void SetUp() override {
    logging_stub::lines().clear();
    for (const auto& blob : TREE_FIXTURE_BLOBS) blobs[blob.shaHex] = blob.content;
  }

  // Whatever the run's outcome, tree.tmp is gone and every file is closed.
  void TearDown() override {
    failNothrowSize = 0;
    EXPECT_FALSE(storage.exists(SYNC_TREE_PATH));
    EXPECT_EQ(storage.openFiles(), 0u);
  }

  GitSyncReport run() {
    const GitSyncTransport transport{this, &fetchTreeFake, &fetchBlobFake};
    const GitSyncHooks hooks{this, &clearFake, &movedFake};
    const GitSyncProgressSink progress{this, &progressFake};
    const size_t linesBefore = logging_stub::lines().size();
    const GitSyncReport report = runGitSync(storage, transport, hooks, progress);
    expectOneEndLineUnlessOk(report, linesBefore);
    return report;
  }

  // Every report other than Ok leaves exactly one error line naming its error; an Ok report leaves none.
  static void expectOneEndLineUnlessOk(const GitSyncReport& report, size_t linesBefore) {
    const auto& lines = logging_stub::lines();
    std::vector<std::string> ended;
    for (size_t i = linesBefore; i < lines.size(); ++i) {
      if (lines[i].level == 'E' && lines[i].origin == "GITSYNC" && lines[i].text.rfind("Sync ended: ", 0) == 0) {
        ended.push_back(lines[i].text);
      }
    }
    if (report.error == GitSyncError::Ok) {
      EXPECT_TRUE(ended.empty()) << ended.front();
      return;
    }
    ASSERT_EQ(ended.size(), 1u) << describeGitSyncError(report.error);
    EXPECT_NE(ended[0].find(describeGitSyncError(report.error)), std::string::npos) << ended[0];
  }

  static bool clearFake(void* ctx, const char* absPath) {
    auto* self = static_cast<GitSyncRunTest*>(ctx);
    self->cleared.push_back(absPath);
    return self->hookResult;
  }

  static bool movedFake(void* ctx, const char* oldAbsPath, const char* newAbsPath) {
    auto* self = static_cast<GitSyncRunTest*>(ctx);
    self->moves.push_back(std::string(oldAbsPath) + " -> " + newAbsPath);
    return self->hookResult;
  }

  // A first sync, then a tree where Dune moved to the root and README.md changed.
  void syncThenMoveAndModify() {
    ASSERT_EQ(run().error, GitSyncError::Ok);
    const std::string readme = "# Books, again\n";
    const std::string readmeSha = hex(blobSha(readme).data());
    blobs[readmeSha] = readme;
    treeBody = replaced(treeBody, R"("path":"Livres/SF/Dune.epub")", R"("path":"Dune.epub")");
    treeBody = replaced(treeBody, R"("sha":"4f9fafdb6017bcf0a8f4eb0bf9b7475e447afa73","size":8)",
                        R"("sha":")" + readmeSha + R"(","size":)" + std::to_string(readme.size()));
    blobFetches.clear();
    cleared.clear();
    moves.clear();
  }

  struct Progress {
    GitSyncPhase phase;
    bool tick;
    uint32_t done;
    uint32_t total;
    std::string repoPath;
    uint32_t treeEntries;
  };

  // Records every call; answers false from the cancelAt-th call on (1-based, 0 never), or from the first call
  // cancelIf accepts.
  static bool progressFake(void* ctx, const GitSyncProgress& progress) {
    auto* self = static_cast<GitSyncRunTest*>(ctx);
    self->progress.push_back({progress.phase, progress.tick, progress.done, progress.total,
                              progress.repoPath ? progress.repoPath : "", progress.treeEntries});
    if (self->cancelIf && self->cancelIf(self->progress.back())) self->cancelAt = self->progress.size();
    return self->cancelAt == 0 || self->progress.size() < self->cancelAt;
  }

  // The non-tick calls, as "phase done/total path".
  std::vector<std::string> changes() const {
    static constexpr const char* NAMES[] = {"Preparing", "FetchingTree", "Planning", "Applying"};
    std::vector<std::string> out;
    for (const Progress& p : progress) {
      if (p.tick) continue;
      std::string line = NAMES[static_cast<int>(p.phase)];
      if (p.phase == GitSyncPhase::Applying) {
        line += " " + std::to_string(p.done) + "/" + std::to_string(p.total) + " " + p.repoPath;
      }
      out.push_back(line);
    }
    return out;
  }

  // Serves the tree body in random chunks of 1 to 64 bytes, as the downloader streams it.
  // Then answers treeStatus when the body was accepted.
  static GitFetchStatus fetchTreeFake(void* ctx, const SyncChunkSink& sink) {
    auto* self = static_cast<GitSyncRunTest*>(ctx);
    const GitFetchStatus served = self->serveChunks(self->treeBody, sink);
    return served == GitFetchStatus::Ok ? self->treeStatus : served;
  }

  static GitFetchStatus fetchBlobFake(void* ctx, const SyncFetchRequest& request, const SyncChunkSink& sink) {
    auto* self = static_cast<GitSyncRunTest*>(ctx);
    const std::string key = hex(request.sha);
    self->blobFetches.push_back(request.repoPath);
    if (!self->blobScript.empty()) {
      const GitFetchStatus scripted = self->blobScript.front();
      self->blobScript.pop_front();
      if (scripted != GitFetchStatus::Ok) return scripted;
    }
    const auto it = self->blobs.find(key);
    if (it == self->blobs.end()) return GitFetchStatus::NetworkError;
    return self->serveChunks(it->second, sink);
  }

  GitFetchStatus serveChunks(const std::string& body, const SyncChunkSink& sink) {
    std::uniform_int_distribution<size_t> chunk(1, 64);
    for (size_t pos = 0; pos < body.size();) {
      const size_t len = std::min(chunk(rng), body.size() - pos);
      bytesServed += len;
      if (!sink.write(sink.ctx, reinterpret_cast<const uint8_t*>(body.data()) + pos, len)) {
        return GitFetchStatus::Failed;
      }
      pos += len;
    }
    return GitFetchStatus::Ok;
  }

  // A first sync, then a tree that would delete README.md.
  void syncThenDropReadme() {
    ASSERT_EQ(run().error, GitSyncError::Ok);
    treeBody = treeWithoutReadme();
    blobFetches.clear();
    bytesServed = 0;
    logging_stub::lines().clear();
  }

  // The files, directories and live manifest entries. The manifest's bytes are left out: prepare() may compact it.
  struct CardState {
    std::map<std::string, std::string> files;
    std::set<std::string> directories;
    std::map<std::string, std::string> live;
    bool operator==(const CardState&) const = default;
    friend void PrintTo(const CardState& state, std::ostream* os) {
      *os << "files:";
      ::PrintTo(state.files, os);
      *os << "\nlive:";
      ::PrintTo(state.live, os);
    }
  };
  CardState cardState() {
    auto snap = storage.snapshot();
    snap.files.erase(SYNC_MANIFEST_PATH);
    std::map<std::string, std::string> live;
    for (const auto& [path, entry] : liveEntries(storage)) live[path] = entry.shaHex;
    return {snap.files, snap.directories, live};
  }

  MemorySyncStorage storage;
  std::mt19937 rng{42};
  std::string treeBody = TREE_FIXTURE_BODY;
  GitFetchStatus treeStatus = GitFetchStatus::Ok;
  std::map<std::string, std::string> blobs;
  std::vector<std::string> blobFetches;
  // Answers for the next blob fetches, in order; Ok serves the blob, anything else fails without a byte.
  std::deque<GitFetchStatus> blobScript;
  size_t bytesServed = 0;
  std::vector<Progress> progress;
  size_t cancelAt = 0;
  std::function<bool(const Progress&)> cancelIf;
  std::vector<std::string> cleared;
  std::vector<std::string> moves;
  bool hookResult = true;
};

}  // namespace

TEST_F(GitSyncRunTest, FirstSyncMirrorsTheRepository) {
  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::Ok);
  EXPECT_EQ(report.run.added, 5u);
  EXPECT_EQ(report.run.fileErrors, 0u);
  EXPECT_EQ(bookFiles(storage), fixtureFiles());
  EXPECT_EQ(liveEntries(storage).size(), 5u);
}

TEST_F(GitSyncRunTest, SecondSyncFetchesNothingAndChangesNothing) {
  ASSERT_EQ(run().error, GitSyncError::Ok);
  blobFetches.clear();
  storage.resetCounters();

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::Ok);
  EXPECT_TRUE(blobFetches.empty());
  EXPECT_EQ(report.run.added + report.run.updated + report.run.moved + report.run.deleted, 0u);
  EXPECT_EQ(bookFiles(storage), fixtureFiles());
  EXPECT_EQ(liveEntries(storage).size(), 5u);
  EXPECT_FALSE(storage.exists(SYNC_TREE_PATH));
  EXPECT_EQ(storage.openFiles(), 0u);
}

TEST_F(GitSyncRunTest, FilesCopiedFromAPcAreAdoptedWithoutFetching) {
  for (const auto& [path, content] : fixtureFiles()) seedFile(storage, path, content);

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::Ok);
  EXPECT_EQ(report.run.adopted, 5u);
  EXPECT_TRUE(blobFetches.empty());
  EXPECT_EQ(bookFiles(storage), fixtureFiles());
  EXPECT_EQ(liveEntries(storage).size(), 5u);
}

TEST_F(GitSyncRunTest, TruncatedTreeChangesNothing) {
  syncThenDropReadme();
  treeBody = replaced(treeBody, R"("truncated":false)", R"("truncated":true)");
  const CardState before = cardState();

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::TreeTruncated);
  EXPECT_EQ(cardState(), before);
}

TEST_F(GitSyncRunTest, TreeCutOffAtAnyByteChangesNothing) {
  syncThenDropReadme();
  const std::string full = treeBody;
  const CardState before = cardState();

  for (size_t cut = 0; cut < full.size(); cut += 37) {
    treeBody = full.substr(0, cut);
    const GitSyncReport report = run();
    EXPECT_EQ(report.error, GitSyncError::TreeInvalid) << "cut=" << cut;
    EXPECT_EQ(cardState(), before) << "cut=" << cut;
  }
}

TEST_F(GitSyncRunTest, TreeFetchFailureChangesNothing) {
  syncThenDropReadme();
  const CardState before = cardState();

  for (const GitFetchStatus status : {GitFetchStatus::NetworkError, GitFetchStatus::Failed}) {
    treeBody.clear();
    treeStatus = status;
    EXPECT_EQ(run().error, GitSyncError::TreeDownloadFailed);
    EXPECT_EQ(cardState(), before);
  }
}

TEST_F(GitSyncRunTest, NetworkErrorAfterPartOfTheTreeIsADownloadFailure) {
  syncThenDropReadme();
  treeBody = treeBody.substr(0, treeBody.size() / 2);
  treeStatus = GitFetchStatus::NetworkError;
  const CardState before = cardState();

  EXPECT_EQ(run().error, GitSyncError::TreeDownloadFailed);
  EXPECT_EQ(cardState(), before);
}

TEST_F(GitSyncRunTest, MalformedTreeStopsTheDownloadAndChangesNothing) {
  syncThenDropReadme();
  treeBody = replaced(treeBody, R"("size":5,)", R"("size":"5",)");
  const CardState before = cardState();

  EXPECT_EQ(run().error, GitSyncError::TreeInvalid);
  EXPECT_LT(bytesServed, treeBody.size());
  EXPECT_EQ(cardState(), before);
}

TEST_F(GitSyncRunTest, TreeRedirectRefusedChangesNothing) {
  syncThenDropReadme();
  treeBody.clear();
  treeStatus = GitFetchStatus::RedirectRefused;
  const CardState before = cardState();

  EXPECT_EQ(run().error, GitSyncError::RedirectRefused);
  EXPECT_EQ(cardState(), before);
}

TEST_F(GitSyncRunTest, TreeOverTheCapIsRefusedDuringTheDownload) {
  syncThenDropReadme();
  treeBody = generatedTree(MAX_SYNC_FILES + 50);
  const CardState before = cardState();

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::TooManyFiles);
  EXPECT_LT(bytesServed, treeBody.size());
  EXPECT_EQ(report.tree.written, MAX_SYNC_FILES);
  EXPECT_EQ(cardState(), before);
}

TEST_F(GitSyncRunTest, ProgressFollowsThePhasesAndEveryOperation) {
  run();

  const std::vector<std::string> expected = {
      "Preparing",
      "FetchingTree",
      "Planning",
      "Applying 1/5 Livres/Mon livre.epub",
      "Applying 2/5 Livres/SF/Dune.epub",
      "Applying 3/5 Livres/vide.txt",
      "Applying 4/5 Livres/\xC3\x89t\xC3\xA9.epub",
      "Applying 5/5 README.md",
  };
  EXPECT_EQ(changes(), expected);
}

TEST_F(GitSyncRunTest, TicksArriveDuringTheTreeDownloadAndEveryBlob) {
  run();

  size_t treeTicks = 0;
  uint32_t lastTreeEntries = 0;
  std::map<uint32_t, size_t> applyTicks;
  for (const Progress& p : progress) {
    if (!p.tick) continue;
    if (p.phase == GitSyncPhase::FetchingTree) {
      ++treeTicks;
      EXPECT_GE(p.treeEntries, lastTreeEntries);
      lastTreeEntries = p.treeEntries;
    } else {
      EXPECT_EQ(p.phase, GitSyncPhase::Applying);
      ++applyTicks[p.done];
    }
  }
  EXPECT_GT(treeTicks, 10u);
  EXPECT_EQ(lastTreeEntries, 5u);
  // Every operation that fetched bytes ticked; the empty file fetched nothing.
  EXPECT_GT(applyTicks[1], 0u);
  EXPECT_GT(applyTicks[2], 0u);
  EXPECT_EQ(applyTicks.count(3), 0u);
  EXPECT_GT(applyTicks[4], 0u);
  EXPECT_GT(applyTicks[5], 0u);
}

TEST_F(GitSyncRunTest, AdoptionHashingTicks) {
  for (const auto& [path, content] : fixtureFiles()) seedFile(storage, path, content);

  run();

  size_t hashTicks = 0;
  for (const Progress& p : progress) hashTicks += p.tick && p.phase == GitSyncPhase::Applying;
  // One per 4 KB chunk of each non-empty file.
  EXPECT_EQ(hashTicks, 4u);
}

TEST_F(GitSyncRunTest, CancelWhilePreparingFetchesNothing) {
  syncThenDropReadme();
  const CardState before = cardState();
  cancelAt = 1;

  EXPECT_EQ(run().error, GitSyncError::Cancelled);
  EXPECT_EQ(bytesServed, 0u);
  EXPECT_EQ(cardState(), before);
}

TEST_F(GitSyncRunTest, CancelDuringTheTreeDownloadStopsItAndChangesNothing) {
  syncThenDropReadme();
  const CardState before = cardState();
  cancelAt = 5;

  EXPECT_EQ(run().error, GitSyncError::Cancelled);
  EXPECT_LT(bytesServed, treeBody.size());
  EXPECT_EQ(cardState(), before);
}

TEST_F(GitSyncRunTest, CancelDuringApplyStopsAfterTheCurrentFileAndARerunFinishes) {
  // A tick while the second file downloads.
  cancelIf = [](const Progress& p) { return p.phase == GitSyncPhase::Applying && p.tick && p.done == 2; };

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::Cancelled);
  EXPECT_EQ(report.run.added, 2u);
  EXPECT_EQ(blobFetches.size(), 2u);
  EXPECT_EQ(fileContent(storage, "/Livres/SF/Dune.epub"), "Dune\n");
  EXPECT_FALSE(storage.exists(SYNC_TREE_PATH));

  cancelIf = nullptr;
  cancelAt = 0;
  EXPECT_EQ(run().error, GitSyncError::Ok);
  EXPECT_EQ(bookFiles(storage), fixtureFiles());
}

TEST_F(GitSyncRunTest, BlobRedirectRefusedStopsTheRunAndARerunFinishes) {
  blobScript = {GitFetchStatus::Ok, GitFetchStatus::RedirectRefused};

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::RedirectRefused);
  EXPECT_EQ(blobFetches.size(), 2u);
  EXPECT_EQ(report.run.added, 1u);
  EXPECT_EQ(report.run.fileErrors, 1u);
  EXPECT_FALSE(storage.exists(SYNC_TREE_PATH));

  EXPECT_EQ(run().error, GitSyncError::Ok);
  EXPECT_EQ(bookFiles(storage), fixtureFiles());
}

TEST_F(GitSyncRunTest, ThreeConsecutiveNetworkErrorsStopTheRunAndARerunFinishes) {
  blobScript = {GitFetchStatus::NetworkError, GitFetchStatus::NetworkError, GitFetchStatus::NetworkError};

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::TooManyFetchFailures);
  EXPECT_EQ(blobFetches.size(), 3u);
  EXPECT_EQ(report.run.fileErrors, 3u);
  EXPECT_FALSE(storage.exists(SYNC_TREE_PATH));

  EXPECT_EQ(run().error, GitSyncError::Ok);
  EXPECT_EQ(bookFiles(storage), fixtureFiles());
}

TEST_F(GitSyncRunTest, AnOkFetchResetsTheNetworkErrorCount) {
  blobScript = {GitFetchStatus::NetworkError, GitFetchStatus::NetworkError, GitFetchStatus::Ok,
                GitFetchStatus::NetworkError};

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::Ok);
  EXPECT_EQ(report.run.fileErrors, 3u);
  EXPECT_EQ(blobFetches.size(), 4u);
}

TEST_F(GitSyncRunTest, RefusedOrAbortedBodiesAreFileErrorsOnly) {
  blobScript = {GitFetchStatus::Failed, GitFetchStatus::Aborted, GitFetchStatus::Failed, GitFetchStatus::Aborted};

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::Ok);
  EXPECT_EQ(report.run.fileErrors, 4u);
  EXPECT_EQ(report.run.added, 1u);
}

TEST_F(GitSyncRunTest, MoveAndModifyReachTheReadingStateHooks) {
  syncThenMoveAndModify();

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::Ok);
  EXPECT_EQ(report.run.moved, 1u);
  EXPECT_EQ(report.run.updated, 1u);
  EXPECT_EQ(moves, std::vector<std::string>{"/Livres/SF/Dune.epub -> /Dune.epub"});
  EXPECT_NE(std::find(cleared.begin(), cleared.end(), "/README.md"), cleared.end());
  EXPECT_EQ(fileContent(storage, "/Dune.epub"), "Dune\n");
  EXPECT_EQ(fileContent(storage, "/README.md"), "# Books, again\n");
}

TEST_F(GitSyncRunTest, FailingHookStopsTheRunAndTheRerunRepeatsIt) {
  syncThenMoveAndModify();
  hookResult = false;

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::StorageError);
  EXPECT_FALSE(storage.exists(SYNC_TREE_PATH));
  hookResult = true;
  moves.clear();
  EXPECT_EQ(run().error, GitSyncError::Ok);
  EXPECT_EQ(moves, std::vector<std::string>{"/Livres/SF/Dune.epub -> /Dune.epub"});
  EXPECT_EQ(fileContent(storage, "/README.md"), "# Books, again\n");
}

TEST_F(GitSyncRunTest, CorruptManifestStopsBeforeTheTreeIsFetched) {
  seedFile(storage, SYNC_MANIFEST_PATH, "not a manifest at all");

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::ManifestCorrupt);
  EXPECT_EQ(bytesServed, 0u);
  EXPECT_EQ(fileContent(storage, SYNC_MANIFEST_PATH), "not a manifest at all");
}

TEST_F(GitSyncRunTest, LeftoverTreeSnapshotIsRemovedBeforeTheTreePhase) {
  syncThenDropReadme();
  seedFile(storage, SYNC_TREE_PATH, "left by an interrupted run");
  cancelAt = 1;

  EXPECT_EQ(run().error, GitSyncError::Cancelled);
  EXPECT_EQ(bytesServed, 0u);
}

TEST_F(GitSyncRunTest, LeftoverTreeSnapshotThatCannotBeRemovedIsAStorageError) {
  syncThenDropReadme();
  const CardState before = cardState();
  // The mutating call that removes the leftover, found on a cancelled run once a first one compacted the manifest.
  cancelAt = 1;
  ASSERT_EQ(run().error, GitSyncError::Cancelled);
  seedFile(storage, SYNC_TREE_PATH, "left by an interrupted run");
  storage.resetCounters();
  progress.clear();
  ASSERT_EQ(run().error, GitSyncError::Cancelled);
  const auto& calls = storage.calls();
  const auto removal = std::find_if(calls.begin(), calls.end(), [](const MemorySyncStorage::Call& call) {
    return call.kind == MemorySyncStorage::CallKind::Remove && call.path == SYNC_TREE_PATH;
  });
  ASSERT_NE(removal, calls.end());
  const size_t removalCall = static_cast<size_t>(removal - calls.begin()) + 1;
  seedFile(storage, SYNC_TREE_PATH, "left by an interrupted run");
  storage.resetCounters();
  storage.failAt(removalCall, MemorySyncStorage::Tear::None);
  cancelAt = 0;
  bytesServed = 0;

  EXPECT_EQ(run().error, GitSyncError::StorageError);
  EXPECT_EQ(bytesServed, 0u);
  storage.clearFault();
  ASSERT_TRUE(storage.remove(SYNC_TREE_PATH));
  EXPECT_EQ(cardState(), before);
}

TEST_F(GitSyncRunTest, EveryAllocationFailureIsOutOfMemoryAndChangesNothing) {
  syncThenDropReadme();
  const CardState before = cardState();
  struct Allocation {
    const char* name;
    size_t bytes;
  };
  const Allocation allocations[] = {{"executor", sizeof(SyncExecutor)},
                                    {"hash buffer", GIT_SYNC_HASH_BUFFER_BYTES},
                                    {"tree writer", sizeof(TreeSnapshotWriter)},
                                    {"planner", sizeof(SyncPlanner)}};

  for (const Allocation& allocation : allocations) {
    logging_stub::lines().clear();
    failNothrowSize = allocation.bytes;
    const GitSyncReport report = run();
    failNothrowSize = 0;

    EXPECT_EQ(report.error, GitSyncError::OutOfMemory) << allocation.name;
    EXPECT_EQ(cardState(), before) << allocation.name;
    EXPECT_FALSE(storage.exists(SYNC_TREE_PATH)) << allocation.name;
    const std::string expected = std::string("OOM: ") + allocation.name + " needs " + std::to_string(allocation.bytes);
    const auto& lines = logging_stub::lines();
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [&](const logging_stub::Line& line) {
      return line.level == 'E' && line.text.rfind(expected, 0) == 0;
    })) << allocation.name;
  }
}

TEST_F(GitSyncRunTest, EndLineNamesTheRunAndRecordStatus) {
  seedFile(storage, SYNC_MANIFEST_PATH, "not a manifest at all");

  const GitSyncReport report = run();

  ASSERT_EQ(report.error, GitSyncError::ManifestCorrupt);
  const std::string expected = "Sync ended: manifest corrupt (run status " +
                               std::to_string(static_cast<unsigned>(report.run.status)) + ", record status " +
                               std::to_string(static_cast<unsigned>(report.run.recordStatus)) + ")";
  EXPECT_EQ(logging_stub::lines().back().text, expected);
}

TEST_F(GitSyncRunTest, TreeFetchFailureIsLoggedWithItsStatus) {
  treeBody.clear();
  treeStatus = GitFetchStatus::NetworkError;

  run();

  const auto& lines = logging_stub::lines();
  EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const logging_stub::Line& line) {
    return line.level == 'E' && line.text == "Tree fetch ended with status 2";
  }));
}

TEST_F(GitSyncRunTest, StorageFaultAnywhereInARunIsRepairedByTheNextRun) {
  // A synced card, then a tree that moves, modifies, deletes, adds one file and adopts another.
  const std::string adopted = "Nouveau\n";
  const std::string added = "Ajout\n";
  const auto seed = [&] {
    storage = MemorySyncStorage();
    treeBody = TREE_FIXTURE_BODY;
    ASSERT_EQ(run().error, GitSyncError::Ok);
    syncThenMoveAndModify();
    const size_t from = treeBody.find(R"({"path":"Livres/Mon livre.epub")");
    treeBody.erase(from, treeBody.find(R"({"path":"Livres/SF")") - from);
    std::string extra;
    for (const std::string* content : {&adopted, &added}) {
      const std::string sha = hex(blobSha(*content).data());
      blobs[sha] = *content;
      extra += R"({"path":")" + std::string(content == &adopted ? "Nouveau.epub" : "Ajout.txt") +
               R"(","mode":"100644","type":"blob","sha":")" + sha + R"(","size":)" + std::to_string(content->size()) +
               "},";
    }
    treeBody = replaced(treeBody, R"({"path":"vendor")", extra + R"({"path":"vendor")");
    seedFile(storage, "/Nouveau.epub", adopted);
    storage.resetCounters();
  };

  seed();
  const GitSyncReport clean = run();
  ASSERT_EQ(clean.error, GitSyncError::Ok);
  ASSERT_EQ(clean.run.fileErrors, 0u);
  ASSERT_EQ(clean.run.adopted, 1u);
  ASSERT_EQ(clean.run.moved, 1u);
  ASSERT_EQ(clean.run.deleted, 1u);
  ASSERT_EQ(clean.run.added, 1u);
  ASSERT_EQ(clean.run.updated, 1u);
  const size_t calls = storage.mutatingCalls();
  ASSERT_GT(calls, 20u);
  const CardState expected = cardState();

  for (size_t n = 1; n <= calls; ++n) {
    for (const auto tear : {MemorySyncStorage::Tear::None, MemorySyncStorage::Tear::Half}) {
      const std::string where = "n=" + std::to_string(n) + (tear == MemorySyncStorage::Tear::Half ? " half" : "");
      seed();
      storage.failAt(n, tear);
      const GitSyncReport interrupted = run();
      EXPECT_TRUE(interrupted.error != GitSyncError::Ok || interrupted.run.fileErrors > 0) << where;
      storage.clearFault();
      // A failing card may keep tree.tmp; the rerun removes it as a leftover.
      EXPECT_EQ(storage.openFiles(), 0u) << where;

      const GitSyncReport rerun = run();
      EXPECT_EQ(rerun.error, GitSyncError::Ok) << where;
      EXPECT_EQ(rerun.run.fileErrors, 0u) << where;
      EXPECT_EQ(cardState(), expected) << where;
    }
  }
}

TEST_F(GitSyncRunTest, NullProgressSinkNeverCancels) {
  const GitSyncTransport transport{this, &fetchTreeFake, &fetchBlobFake};
  const GitSyncHooks hooks{nullptr, nullptr, nullptr};

  EXPECT_EQ(runGitSync(storage, transport, hooks, GitSyncProgressSink{nullptr, nullptr}).error, GitSyncError::Ok);
  EXPECT_EQ(bookFiles(storage), fixtureFiles());
}

TEST_F(GitSyncRunTest, RedirectRefusedOutranksTheUsersCancel) {
  cancelIf = [](const Progress& p) { return p.phase == GitSyncPhase::Applying && !p.tick && p.done == 1; };
  blobScript = {GitFetchStatus::RedirectRefused};

  const GitSyncReport report = run();

  EXPECT_EQ(report.error, GitSyncError::RedirectRefused);
  EXPECT_EQ(blobFetches.size(), 1u);
}

TEST_F(GitSyncRunTest, MissingTransportFunctionsAreRefusedNotCalled) {
  const GitSyncHooks hooks{nullptr, nullptr, nullptr};
  const GitSyncProgressSink progress{nullptr, nullptr};

  const GitSyncTransport noTree{this, nullptr, &fetchBlobFake};
  EXPECT_EQ(runGitSync(storage, noTree, hooks, progress).error, GitSyncError::TreeDownloadFailed);

  const GitSyncTransport noBlob{this, &fetchTreeFake, nullptr};
  const GitSyncReport report = runGitSync(storage, noBlob, hooks, progress);
  EXPECT_EQ(report.error, GitSyncError::Ok);
  EXPECT_EQ(report.run.fileErrors, 4u);
}
