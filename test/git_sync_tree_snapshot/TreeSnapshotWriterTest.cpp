#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Logging.h"
#include "MemoryRecordStream.h"
#include "RecordReader.h"
#include "SyncPlanner.h"
#include "TreeFixture.h"
#include "TreeSnapshotWriter.h"

namespace {

std::string shaHex(const uint8_t sha[GitBlobHasher::SHA_SIZE]) {
  char hex[SHA_HEX_LENGTH + 1];
  shaToHex(sha, hex);
  return hex;
}

struct Entry {
  std::string path;
  std::string sha;
  uint64_t size;

  bool operator==(const Entry&) const = default;
};

std::ostream& operator<<(std::ostream& os, const Entry& e) {
  return os << "{" << e.path << ", " << e.sha << ", " << e.size << "}";
}

// What a tree file reads back as: its entries and the status after them (End only when sealed).
struct ReadBack {
  std::vector<Entry> entries;
  RecordStatus end;
};

ReadBack readBack(const uint8_t* data, size_t len) {
  std::vector<uint8_t> copy(data, data + len);
  MemoryRecordStream source(copy.data(), copy.size(), copy.size());
  auto reader = std::make_unique<RecordReader>(source, RecordFileKind::Tree);
  ReadBack result;
  SyncRecord record;
  RecordStatus status;
  while ((status = reader->next(record)) == RecordStatus::Record) {
    EXPECT_EQ(record.replaces, NO_OFFSET);
    result.entries.push_back({std::string(record.path, record.pathLen), shaHex(record.sha), record.size});
  }
  result.end = status;
  return result;
}

std::vector<Entry> fixtureSyncableEntries() {
  std::vector<Entry> entries;
  for (const auto& blob : TREE_FIXTURE_BLOBS) {
    if (blob.path[0] != '.') entries.push_back({blob.path, blob.shaHex, blob.size});
  }
  return entries;
}

// A writer over an in-memory tree file.
struct Harness {
  std::vector<uint8_t> buffer = std::vector<uint8_t>(512 * 1024);
  MemoryRecordStream file{buffer.data(), buffer.size()};
  std::unique_ptr<TreeSnapshotWriter> writer = std::make_unique<TreeSnapshotWriter>(file);

  ReadBack output() const { return readBack(file.data(), file.length()); }
  std::vector<uint8_t> bytes() const { return {file.data(), file.data() + file.length()}; }

  // begin(), the body in chunks of the given sizes (cycled), then finish().
  TreeSnapshotStatus run(const std::string& body, const std::vector<size_t>& chunks) {
    EXPECT_TRUE(writer->begin());
    size_t pos = 0;
    for (size_t i = 0; pos < body.size(); ++i) {
      const size_t len = std::min(chunks[i % chunks.size()], body.size() - pos);
      writer->feed(body.data() + pos, len);
      pos += len;
    }
    return writer->finish();
  }
};

std::string replaced(std::string text, const std::string& from, const std::string& to) {
  const size_t at = text.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  if (at != std::string::npos) text.replace(at, from.size(), to);
  return text;
}

// One blob of a generated body; jsonPath is the path as it appears between the JSON quotes.
struct BodyBlob {
  std::string jsonPath;
  std::string sha;
};

std::string blobJson(const BodyBlob& blob) {
  return R"({"path":")" + blob.jsonPath + R"(","mode":"100644","type":"blob","sha":")" + blob.sha + R"(","size":3})";
}

std::string treeBody(const std::vector<BodyBlob>& blobs) {
  std::string body = R"({"sha":")" + std::string(TREE_FIXTURE_SHA_HEX) + R"(","tree":[)";
  for (size_t i = 0; i < blobs.size(); ++i) {
    if (i > 0) body += ',';
    body += blobJson(blobs[i]);
  }
  return body + R"(],"truncated":false})";
}

// A 40-hex SHA made of one repeated digit.
std::string sha(char digit) { return std::string(SHA_HEX_LENGTH, digit); }

bool logged(const std::string& text) {
  for (const std::string& line : logging_stub::lines()) {
    if (line.find(text) != std::string::npos) return true;
  }
  return false;
}

TEST(TreeSnapshotWriter, WholeFixtureIsWrittenAsItsSyncableBlobsSealed) {
  Harness h;
  ASSERT_TRUE(h.writer->begin());
  const std::string body = TREE_FIXTURE_BODY;
  h.writer->feed(body.data(), body.size());
  EXPECT_FALSE(h.writer->stopped());

  EXPECT_EQ(h.writer->finish(), TreeSnapshotStatus::Ok);
  const ReadBack out = h.output();
  EXPECT_EQ(out.end, RecordStatus::End);
  EXPECT_EQ(out.entries, fixtureSyncableEntries());
  EXPECT_EQ(h.writer->entriesWritten(), fixtureSyncableEntries().size());
  EXPECT_EQ(shaHex(h.writer->treeSha()), TREE_FIXTURE_SHA_HEX);
}

TEST(TreeSnapshotWriter, OutputIsByteIdenticalWhateverTheChunking) {
  Harness whole;
  ASSERT_EQ(whole.run(TREE_FIXTURE_BODY, {SIZE_MAX}), TreeSnapshotStatus::Ok);

  Harness byteByByte;
  ASSERT_EQ(byteByByte.run(TREE_FIXTURE_BODY, {1}), TreeSnapshotStatus::Ok);
  EXPECT_EQ(byteByByte.bytes(), whole.bytes());

  std::mt19937 rng(9);
  std::uniform_int_distribution<size_t> size(1, 97);
  for (int round = 0; round < 20; ++round) {
    std::vector<size_t> chunks(64);
    for (size_t& chunk : chunks) chunk = size(rng);
    Harness random;
    ASSERT_EQ(random.run(TREE_FIXTURE_BODY, chunks), TreeSnapshotStatus::Ok) << "round " << round;
    EXPECT_EQ(random.bytes(), whole.bytes()) << "round " << round;
  }
}

TEST(TreeSnapshotWriter, TruncatedTreeIsRefusedAndLeftUnsealed) {
  Harness h;
  const std::string body = replaced(TREE_FIXTURE_BODY, R"("truncated":false)", R"("truncated":true)");

  EXPECT_EQ(h.run(body, {13}), TreeSnapshotStatus::Truncated);
  EXPECT_EQ(h.output().end, RecordStatus::Truncated);
}

TEST(TreeSnapshotWriter, BodyCutAtAnyByteIsInvalidUnsealedAndLogged) {
  const std::string body = TREE_FIXTURE_BODY;
  for (size_t cut = 0; cut < body.size(); ++cut) {
    Harness h;
    logging_stub::lines().clear();
    ASSERT_EQ(h.run(body.substr(0, cut), {7}), TreeSnapshotStatus::Invalid) << "cut at " << cut;
    EXPECT_NE(h.output().end, RecordStatus::End) << "cut at " << cut;
    EXPECT_FALSE(logging_stub::lines().empty()) << "cut at " << cut;
  }
}

TEST(TreeSnapshotWriter, ParserErrorIsInvalid) {
  Harness h;
  const std::string body = replaced(TREE_FIXTURE_BODY, R"("size":5,)", R"("size":"5",)");

  EXPECT_EQ(h.run(body, {64}), TreeSnapshotStatus::Invalid);
  EXPECT_NE(h.output().end, RecordStatus::End);
}

TEST(TreeSnapshotWriter, UnsyncablePathsAreCountedNotWrittenAndLoggedBySha) {
  const std::string tooLong = "Secret/" + std::string(MAX_SYNC_PATH_BYTES, 'L') + ".epub";
  const std::string overTokenBuffer = "Secret/" + std::string(StreamingJsonParser::TOKEN_BUF_SIZE, 'T') + ".epub";
  const std::vector<BodyBlob> blobs = {
      {".github/Secret.yml", sha('1')}, {"docs/.Secret/x.epub", sha('2')}, {"Secret//empty.epub", sha('3')},
      {"../Secret.epub", sha('4')},     {"/Secret.epub", sha('5')},        {tooLong, sha('6')},
      {overTokenBuffer, sha('7')},      {"Secret\\\\name.epub", sha('8')}, {"Secret\\nline.epub", sha('a')},
      {"Livres/kept.epub", sha('9')},
  };
  Harness h;
  logging_stub::lines().clear();

  ASSERT_EQ(h.run(treeBody(blobs), {11}), TreeSnapshotStatus::Ok);
  EXPECT_EQ(h.output().entries, (std::vector<Entry>{{"Livres/kept.epub", sha('9'), 3}}));
  EXPECT_EQ(h.writer->entriesWritten(), 1u);
  EXPECT_EQ(h.writer->hiddenCount(), 2u);
  EXPECT_EQ(h.writer->malformedCount(), 4u);
  EXPECT_EQ(h.writer->tooLongCount(), 1u);
  EXPECT_EQ(h.writer->skippedLongPathCount(), 1u);
  EXPECT_EQ(h.writer->skippedEscapedPathCount(), 1u);

  EXPECT_FALSE(logged(sha('1')));
  EXPECT_FALSE(logged(sha('2')));
  for (char digit : {'3', '4', '5', '6', 'a'}) EXPECT_TRUE(logged(sha(digit))) << digit;
  EXPECT_FALSE(logged("Secret"));
}

std::vector<BodyBlob> generatedBlobs(size_t count) {
  std::vector<BodyBlob> blobs;
  blobs.reserve(count);
  for (size_t i = 0; i < count; ++i) blobs.push_back({"Books/" + std::to_string(i) + ".epub", sha('c')});
  return blobs;
}

TEST(TreeSnapshotWriter, TreeAtTheFileCapIsAcceptedWhateverItsUnsyncablePaths) {
  Harness h;

  std::vector<BodyBlob> blobs = generatedBlobs(MAX_SYNC_FILES);
  blobs.push_back({".github/ci.yml", sha('d')});
  blobs.push_back({"bad//path.epub", sha('e')});

  ASSERT_EQ(h.run(treeBody(blobs), {4096}), TreeSnapshotStatus::Ok);
  EXPECT_EQ(h.output().entries.size(), MAX_SYNC_FILES);
}

TEST(TreeSnapshotWriter, TreeOverTheFileCapStopsAtTheFirstEntryPastIt) {
  const std::vector<BodyBlob> blobs = generatedBlobs(MAX_SYNC_FILES + 2);
  Harness h;
  logging_stub::lines().clear();
  ASSERT_TRUE(h.writer->begin());
  const std::string head = R"({"sha":")" + std::string(TREE_FIXTURE_SHA_HEX) + R"(","tree":[)";
  h.writer->feed(head.data(), head.size());
  for (size_t i = 0; i < blobs.size(); ++i) {
    const std::string entry = (i > 0 ? "," : "") + blobJson(blobs[i]);
    h.writer->feed(entry.data(), entry.size());
    ASSERT_EQ(h.writer->stopped(), i >= MAX_SYNC_FILES) << "entry " << i;
  }

  EXPECT_EQ(h.writer->finish(), TreeSnapshotStatus::TooManyFiles);
  EXPECT_EQ(h.writer->entriesWritten(), MAX_SYNC_FILES);
  EXPECT_NE(h.output().end, RecordStatus::End);
  EXPECT_FALSE(logging_stub::lines().empty());
}

// Fails its failAt-th write (1-based) and every later one.
class FailingSink : public RecordSink {
 public:
  explicit FailingSink(size_t failAt) : failAt(failAt) {}
  bool write(const uint8_t*, size_t) override { return ++writes < failAt; }
  size_t writes = 0;

 private:
  size_t failAt;
};

TEST(TreeSnapshotWriter, SinkFailingAtAnyWriteGivesWriteFailed) {
  // Header, one frame per syncable blob, seal.
  const size_t totalWrites = fixtureSyncableEntries().size() + 2;
  const std::string body = TREE_FIXTURE_BODY;
  for (size_t failAt = 1; failAt <= totalWrites; ++failAt) {
    FailingSink sink(failAt);
    auto writer = std::make_unique<TreeSnapshotWriter>(sink);
    logging_stub::lines().clear();
    EXPECT_EQ(writer->begin(), failAt > 1) << "write " << failAt;
    writer->feed(body.data(), body.size());
    EXPECT_EQ(writer->stopped(), failAt < totalWrites) << "write " << failAt;

    EXPECT_EQ(writer->finish(), TreeSnapshotStatus::WriteFailed) << "write " << failAt;
    EXPECT_FALSE(logging_stub::lines().empty()) << "write " << failAt;
  }
}

TEST(TreeSnapshotWriter, CancelStopsAtOnceAndIgnoresTheRestOfTheBody) {
  Harness h;
  logging_stub::lines().clear();
  ASSERT_TRUE(h.writer->begin());
  const std::string body = TREE_FIXTURE_BODY;
  const size_t half = body.size() / 2;
  h.writer->feed(body.data(), half);
  const size_t writtenBeforeCancel = h.writer->entriesWritten();

  h.writer->cancel();
  EXPECT_TRUE(h.writer->stopped());
  h.writer->feed(body.data() + half, body.size() - half);

  EXPECT_EQ(h.writer->finish(), TreeSnapshotStatus::Cancelled);
  EXPECT_EQ(h.writer->entriesWritten(), writtenBeforeCancel);
  EXPECT_NE(h.output().end, RecordStatus::End);
  EXPECT_FALSE(logging_stub::lines().empty());
}

TEST(TreeSnapshotWriter, CapStopOutranksALaterCancel) {
  Harness h;
  ASSERT_TRUE(h.writer->begin());
  const std::string body = treeBody(generatedBlobs(MAX_SYNC_FILES + 1));
  h.writer->feed(body.data(), body.size());
  h.writer->cancel();

  EXPECT_EQ(h.writer->finish(), TreeSnapshotStatus::TooManyFiles);
}

TEST(TreeSnapshotWriter, CancelOutranksAnInvalidBody) {
  Harness h;
  ASSERT_TRUE(h.writer->begin());
  const std::string body = TREE_FIXTURE_BODY;
  h.writer->feed(body.data(), 100);
  h.writer->cancel();

  EXPECT_EQ(h.writer->finish(), TreeSnapshotStatus::Cancelled);
}

TEST(TreeSnapshotWriter, BodyFedWithoutBeginIsRefused) {
  Harness h;
  logging_stub::lines().clear();
  const std::string body = TREE_FIXTURE_BODY;
  h.writer->feed(body.data(), body.size());
  EXPECT_TRUE(h.writer->stopped());

  EXPECT_NE(h.writer->finish(), TreeSnapshotStatus::Ok);
  EXPECT_EQ(h.file.length(), 0u);
  EXPECT_FALSE(logging_stub::lines().empty());
}

}  // namespace
