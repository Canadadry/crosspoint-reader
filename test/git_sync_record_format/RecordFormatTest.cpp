#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Crc32.h"
#include "ManifestCompactor.h"
#include "MemoryRecordStream.h"
#include "RecordReader.h"
#include "RecordWriter.h"

namespace {

const uint8_t* bytes(const char* text) { return reinterpret_cast<const uint8_t*>(text); }

constexpr size_t FILE_CAPACITY = 16384;

void fillSha(uint8_t sha[GitBlobHasher::SHA_SIZE], uint8_t seed) {
  for (size_t i = 0; i < GitBlobHasher::SHA_SIZE; ++i) {
    sha[i] = static_cast<uint8_t>(seed + i * 13);
  }
}

struct TestEntry {
  std::string path;
  uint64_t size;
  uint8_t sha[GitBlobHasher::SHA_SIZE];
};

// Covers 1- and 255-byte paths, UTF-8, sizes 0, above 4 GiB and UINT64_MAX, and every SHA byte value.
std::vector<TestEntry> sampleEntries() {
  const std::string paths[] = {"a",
                               std::string(MAX_SYNC_PATH_BYTES, 'x'),
                               "Livres/\xC3\x89t\xC3\xA9.epub",
                               "Auteurs/Hugo/Les Mis\xC3\xA9rables.epub",
                               "empty.txt",
                               "big.bin",
                               "huge.bin"};
  const uint64_t sizes[] = {1, 300000, 812345, 42, 0, 5000000000ull, UINT64_MAX};
  constexpr size_t COUNT = 13;  // 13 * 20 SHA bytes >= 256 values
  std::vector<TestEntry> entries;
  entries.reserve(COUNT);
  for (size_t i = 0; i < COUNT; ++i) {
    TestEntry entry{i < 7 ? paths[i] : "books/b" + std::to_string(i) + ".epub", i < 7 ? sizes[i] : i * 1000, {}};
    for (size_t b = 0; b < GitBlobHasher::SHA_SIZE; ++b) {
      entry.sha[b] = static_cast<uint8_t>(i * GitBlobHasher::SHA_SIZE + b);
    }
    entries.push_back(entry);
  }
  return entries;
}

struct WrittenTree {
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> offsets;
};

WrittenTree writeTree(const std::vector<TestEntry>& entries) {
  WrittenTree tree;
  tree.bytes.resize(FILE_CAPACITY);
  MemoryRecordStream stream(tree.bytes.data(), tree.bytes.size());
  RecordWriter writer(stream);
  EXPECT_TRUE(writer.begin(RecordFileKind::Tree));
  for (const TestEntry& entry : entries) {
    uint32_t offset = 0;
    EXPECT_TRUE(writer.put(entry.sha, entry.size, entry.path.data(), entry.path.size(), NO_OFFSET, &offset));
    tree.offsets.push_back(offset);
  }
  EXPECT_TRUE(writer.seal());
  tree.bytes.resize(stream.length());
  return tree;
}

// Reads to the first status other than Record; the records read before it go to out.
RecordStatus readAll(RecordSource& source, RecordFileKind kind, std::vector<TestEntry>& out,
                     std::vector<uint32_t>* offsets = nullptr) {
  RecordReader reader(source, kind);
  SyncRecord record;
  RecordStatus status;
  while ((status = reader.next(record)) == RecordStatus::Record) {
    TestEntry entry{std::string(record.path, record.pathLen), record.size, {}};
    memcpy(entry.sha, record.sha, sizeof(entry.sha));
    EXPECT_EQ(strlen(record.path), record.pathLen);
    out.push_back(entry);
    if (offsets) offsets->push_back(record.offset);
  }
  return status;
}

RecordStatus readAll(std::vector<uint8_t>& bytes, RecordFileKind kind, std::vector<TestEntry>& out,
                     size_t length = SIZE_MAX) {
  MemoryRecordStream stream(bytes.data(), bytes.size(), length);
  return readAll(stream, kind, out);
}

void expectSameEntries(const std::vector<TestEntry>& actual, const std::vector<TestEntry>& expected, size_t count) {
  ASSERT_EQ(actual.size(), count);
  for (size_t i = 0; i < count; ++i) {
    EXPECT_EQ(actual[i].path, expected[i].path) << i;
    EXPECT_EQ(actual[i].size, expected[i].size) << i;
    EXPECT_EQ(memcmp(actual[i].sha, expected[i].sha, GitBlobHasher::SHA_SIZE), 0) << i;
  }
}

// Hand-built frames carry a valid CRC, so only the grammar check can reject them.
void appendLe(std::vector<uint8_t>& bytes, uint64_t value, size_t count) {
  for (size_t i = 0; i < count; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

std::vector<uint8_t> fileHeader(RecordFileKind kind) {
  return {'G', 'S', 'R', 'F', RECORD_FILE_VERSION, static_cast<uint8_t>(kind), 0, 0};
}

void appendFrame(std::vector<uint8_t>& bytes, uint8_t kind, const std::vector<uint8_t>& body) {
  std::vector<uint8_t> head{kind};
  appendLe(head, body.size(), 2);
  const uint32_t crc = Crc32::update(Crc32::compute(head.data(), head.size()), body.data(), body.size());
  bytes.insert(bytes.end(), head.begin(), head.end());
  appendLe(bytes, crc, 4);
  bytes.insert(bytes.end(), body.begin(), body.end());
}

std::vector<uint8_t> entryBody(const std::string& path, uint32_t replaces = NO_OFFSET) {
  std::vector<uint8_t> body(GitBlobHasher::SHA_SIZE, 0xAB);
  appendLe(body, 99, 8);
  appendLe(body, replaces, 4);
  body.insert(body.end(), path.begin(), path.end());
  return body;
}

std::vector<uint8_t> u32Body(uint32_t value) {
  std::vector<uint8_t> body;
  appendLe(body, value, 4);
  return body;
}

RecordStatus readStatus(std::vector<uint8_t>& bytes, RecordFileKind kind) {
  std::vector<TestEntry> read;
  return readAll(bytes, kind, read);
}

}  // namespace

TEST(Crc32Test, MatchesIeeeCheckValue) { EXPECT_EQ(Crc32::compute(bytes("123456789"), 9), 0xCBF43926u); }

TEST(Crc32Test, EmptyInputIsZero) { EXPECT_EQ(Crc32::compute(nullptr, 0), 0u); }

TEST(Crc32Test, IncrementalEqualsOneShotOverUnevenSplits) {
  const char* text = "The quick brown fox jumps over the lazy dog";
  const size_t len = strlen(text);
  const uint32_t whole = Crc32::compute(bytes(text), len);
  for (size_t first = 0; first <= len; ++first) {
    for (size_t second = first; second <= len; second += 7) {
      uint32_t crc = Crc32::update(0, bytes(text), first);
      crc = Crc32::update(crc, bytes(text) + first, second - first);
      crc = Crc32::update(crc, bytes(text) + second, len - second);
      EXPECT_EQ(crc, whole) << first << "/" << second;
    }
  }
}

TEST(RecordFileTest, TreeWithOneEntryRoundTrips) {
  uint8_t file[FILE_CAPACITY];
  MemoryRecordStream stream(file, sizeof(file));
  RecordWriter writer(stream);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  fillSha(sha, 7);
  uint32_t offset = 0;
  ASSERT_TRUE(writer.begin(RecordFileKind::Tree));
  ASSERT_TRUE(writer.put(sha, 1234, "a/b.epub", 8, NO_OFFSET, &offset));
  ASSERT_TRUE(writer.seal());

  RecordReader reader(stream, RecordFileKind::Tree);
  SyncRecord record;
  ASSERT_EQ(reader.next(record), RecordStatus::Record);
  EXPECT_EQ(record.kind, RecordFrameKind::Entry);
  EXPECT_EQ(record.offset, offset);
  EXPECT_EQ(record.offset, RECORD_HEADER_SIZE);
  EXPECT_FALSE(record.inJournal);
  EXPECT_EQ(memcmp(record.sha, sha, sizeof(sha)), 0);
  EXPECT_EQ(record.size, 1234u);
  EXPECT_EQ(record.replaces, NO_OFFSET);
  EXPECT_EQ(record.pathLen, 8u);
  EXPECT_STREQ(record.path, "a/b.epub");
  EXPECT_EQ(reader.next(record), RecordStatus::End);
}

TEST(RecordFileTest, TreeRoundTripsEdgePathsSizesAndShaBytes) {
  const std::vector<TestEntry> entries = sampleEntries();
  WrittenTree tree = writeTree(entries);
  std::vector<TestEntry> read;
  EXPECT_EQ(readAll(tree.bytes, RecordFileKind::Tree, read), RecordStatus::End);
  expectSameEntries(read, entries, entries.size());

  bool seen[256] = {};
  for (const TestEntry& entry : entries) {
    for (uint8_t b : entry.sha) seen[b] = true;
  }
  for (bool byteSeen : seen) EXPECT_TRUE(byteSeen);
}

TEST(RecordFileTest, EmptyTreeReadsEndWithNoRecord) {
  WrittenTree tree = writeTree({});
  EXPECT_EQ(tree.bytes.size(), RECORD_HEADER_SIZE + FRAME_HEADER_SIZE + OFFSET_BODY_SIZE);
  std::vector<TestEntry> read;
  EXPECT_EQ(readAll(tree.bytes, RecordFileKind::Tree, read), RecordStatus::End);
  EXPECT_TRUE(read.empty());
}

TEST(RecordFileTest, ReaderReportsTheOffsetsPutReturned) {
  WrittenTree tree = writeTree(sampleEntries());
  MemoryRecordStream stream(tree.bytes.data(), tree.bytes.size(), tree.bytes.size());
  std::vector<TestEntry> read;
  std::vector<uint32_t> offsets;
  EXPECT_EQ(readAll(stream, RecordFileKind::Tree, read, &offsets), RecordStatus::End);
  EXPECT_EQ(offsets, tree.offsets);
  uint32_t expected = RECORD_HEADER_SIZE;
  for (size_t i = 0; i < offsets.size(); ++i) {
    EXPECT_EQ(offsets[i], expected);
    expected += FRAME_HEADER_SIZE + ENTRY_FIXED_BODY_SIZE + read[i].path.size();
  }
}

TEST(RecordFileTest, EndRepeatsOnEveryLaterCall) {
  WrittenTree tree = writeTree(sampleEntries());
  MemoryRecordStream stream(tree.bytes.data(), tree.bytes.size(), tree.bytes.size());
  RecordReader reader(stream, RecordFileKind::Tree);
  SyncRecord record;
  while (reader.next(record) == RecordStatus::Record) {
  }
  EXPECT_EQ(reader.next(record), RecordStatus::End);
  EXPECT_EQ(reader.next(record), RecordStatus::End);
}

TEST(RecordFileTest, TreeCutAtEveryByteOffsetIsTruncated) {
  const std::vector<TestEntry> entries = sampleEntries();
  WrittenTree tree = writeTree(entries);
  for (size_t k = 0; k < tree.bytes.size(); ++k) {
    SCOPED_TRACE(k);
    std::vector<TestEntry> read;
    EXPECT_EQ(readAll(tree.bytes, RecordFileKind::Tree, read, k), RecordStatus::Truncated);
    size_t complete = 0;
    while (complete < tree.offsets.size() &&
           tree.offsets[complete] + FRAME_HEADER_SIZE + ENTRY_FIXED_BODY_SIZE + entries[complete].path.size() <= k) {
      ++complete;
    }
    expectSameEntries(read, entries, complete);
  }
}

TEST(RecordFileTest, OtherVersionIsWrongVersionBeforeAnyRecord) {
  for (uint8_t version : {uint8_t{0}, uint8_t{2}, uint8_t{255}}) {
    SCOPED_TRACE(version);
    WrittenTree tree = writeTree(sampleEntries());
    tree.bytes[4] = version;
    std::vector<TestEntry> read;
    EXPECT_EQ(readAll(tree.bytes, RecordFileKind::Tree, read), RecordStatus::WrongVersion);
    EXPECT_TRUE(read.empty());
  }
}

TEST(RecordFileTest, BadMagicIsCorrupt) {
  for (size_t i = 0; i < 4; ++i) {
    SCOPED_TRACE(i);
    WrittenTree tree = writeTree(sampleEntries());
    tree.bytes[i] ^= 0x20;
    std::vector<TestEntry> read;
    EXPECT_EQ(readAll(tree.bytes, RecordFileKind::Tree, read), RecordStatus::Corrupt);
    EXPECT_TRUE(read.empty());
  }
}

TEST(RecordFileTest, TreeReadAsManifestIsCorrupt) {
  WrittenTree tree = writeTree(sampleEntries());
  std::vector<TestEntry> read;
  EXPECT_EQ(readAll(tree.bytes, RecordFileKind::Manifest, read), RecordStatus::Corrupt);
  EXPECT_TRUE(read.empty());
}

TEST(RecordFileTest, ManifestReadAsTreeIsCorrupt) {
  uint8_t file[FILE_CAPACITY];
  MemoryRecordStream stream(file, sizeof(file));
  RecordWriter writer(stream);
  ASSERT_TRUE(writer.begin(RecordFileKind::Manifest));
  ASSERT_TRUE(writer.seal());
  RecordReader reader(stream, RecordFileKind::Tree);
  SyncRecord record;
  EXPECT_EQ(reader.next(record), RecordStatus::Corrupt);
}

TEST(RecordFileTest, NonZeroReservedHeaderByteIsCorrupt) {
  for (size_t i : {6, 7}) {
    SCOPED_TRACE(i);
    WrittenTree tree = writeTree(sampleEntries());
    tree.bytes[i] = 1;
    std::vector<TestEntry> read;
    EXPECT_EQ(readAll(tree.bytes, RecordFileKind::Tree, read), RecordStatus::Corrupt);
  }
}

TEST(RecordFileTest, FlippedBitAnywhereInTreeNeverReadsEnd) {
  std::vector<TestEntry> entries = sampleEntries();
  entries.resize(3);
  const WrittenTree tree = writeTree(entries);
  for (size_t i = 0; i < tree.bytes.size(); ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      SCOPED_TRACE(testing::Message() << "byte " << i << " bit " << bit);
      std::vector<uint8_t> bytes = tree.bytes;
      bytes[i] ^= static_cast<uint8_t>(1u << bit);
      std::vector<TestEntry> read;
      const RecordStatus status = readAll(bytes, RecordFileKind::Tree, read);
      EXPECT_TRUE(status == RecordStatus::Corrupt || status == RecordStatus::Truncated ||
                  status == RecordStatus::WrongVersion)
          << static_cast<int>(status);
    }
  }
}

TEST(RecordFileTest, HandBuiltValidTreeReadsEnd) {
  std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Tree);
  appendFrame(bytes, 'P', entryBody("a.epub"));
  appendFrame(bytes, 'S', u32Body(1));
  EXPECT_EQ(readStatus(bytes, RecordFileKind::Tree), RecordStatus::End);
}

TEST(RecordFileTest, SealCountMismatchIsCorrupt) {
  for (uint32_t count : {0u, 2u, 0xFFFFFFFFu}) {
    std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Tree);
    appendFrame(bytes, 'P', entryBody("a.epub"));
    appendFrame(bytes, 'S', u32Body(count));
    EXPECT_EQ(readStatus(bytes, RecordFileKind::Tree), RecordStatus::Corrupt) << count;
  }
}

TEST(RecordFileTest, AnythingAfterTreeSealIsCorrupt) {
  std::vector<uint8_t> entryAfter = fileHeader(RecordFileKind::Tree);
  appendFrame(entryAfter, 'S', u32Body(0));
  appendFrame(entryAfter, 'P', entryBody("a.epub"));
  EXPECT_EQ(readStatus(entryAfter, RecordFileKind::Tree), RecordStatus::Corrupt);

  std::vector<uint8_t> secondSeal = fileHeader(RecordFileKind::Tree);
  appendFrame(secondSeal, 'S', u32Body(0));
  appendFrame(secondSeal, 'S', u32Body(0));
  EXPECT_EQ(readStatus(secondSeal, RecordFileKind::Tree), RecordStatus::Corrupt);

  std::vector<uint8_t> strayByte = fileHeader(RecordFileKind::Tree);
  appendFrame(strayByte, 'S', u32Body(0));
  strayByte.push_back(0);
  EXPECT_EQ(readStatus(strayByte, RecordFileKind::Tree), RecordStatus::Corrupt);
}

TEST(RecordFileTest, DropInTreeIsCorrupt) {
  std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Tree);
  appendFrame(bytes, 'P', entryBody("a.epub"));
  appendFrame(bytes, 'D', u32Body(RECORD_HEADER_SIZE));
  appendFrame(bytes, 'S', u32Body(1));
  EXPECT_EQ(readStatus(bytes, RecordFileKind::Tree), RecordStatus::Corrupt);
}

TEST(RecordFileTest, ReplacesInTreeIsCorrupt) {
  std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Tree);
  appendFrame(bytes, 'P', entryBody("a.epub"));
  appendFrame(bytes, 'P', entryBody("b.epub", RECORD_HEADER_SIZE));
  appendFrame(bytes, 'S', u32Body(2));
  EXPECT_EQ(readStatus(bytes, RecordFileKind::Tree), RecordStatus::Corrupt);
}

TEST(RecordFileTest, UnknownFrameKindIsCorrupt) {
  for (uint8_t kind : {uint8_t{0}, uint8_t{'p'}, uint8_t{'X'}, uint8_t{0xFF}}) {
    std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Tree);
    appendFrame(bytes, kind, entryBody("a.epub"));
    appendFrame(bytes, 'S', u32Body(1));
    EXPECT_EQ(readStatus(bytes, RecordFileKind::Tree), RecordStatus::Corrupt) << int(kind);
  }
}

TEST(RecordFileTest, BodyLengthNotFittingFrameKindIsCorrupt) {
  std::vector<uint8_t> shortEntry = entryBody("");
  shortEntry.pop_back();
  for (const std::vector<uint8_t>& body : {shortEntry, entryBody(""), entryBody(std::string(256, 'x'))}) {
    std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Tree);
    appendFrame(bytes, 'P', body);
    appendFrame(bytes, 'S', u32Body(1));
    EXPECT_EQ(readStatus(bytes, RecordFileKind::Tree), RecordStatus::Corrupt) << body.size();
  }
  for (size_t sealLen : {0, 3, 5}) {
    std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Tree);
    appendFrame(bytes, 'S', std::vector<uint8_t>(sealLen, 0));
    EXPECT_EQ(readStatus(bytes, RecordFileKind::Tree), RecordStatus::Corrupt) << sealLen;
  }
}

namespace {

// Serves at most one byte per read, from a buffer starting at an odd address.
class OneByteSource : public RecordSource {
 public:
  OneByteSource(const uint8_t* data, size_t length) : data(data), length(length) {}
  int read(uint8_t* buf, size_t len) override {
    if (len == 0 || cursor == length) return 0;
    buf[0] = data[cursor++];
    return 1;
  }
  bool seek(uint32_t offset) override {
    if (offset > length) return false;
    cursor = offset;
    return true;
  }

 private:
  const uint8_t* data;
  size_t length;
  size_t cursor = 0;
};

// Fails every read from the failAt-th byte on.
class FailingSource : public MemoryRecordStream {
 public:
  FailingSource(uint8_t* buffer, size_t length, size_t failAt)
      : MemoryRecordStream(buffer, length, length), failAt(failAt) {}
  int read(uint8_t* buf, size_t len) override {
    if (consumed >= failAt) return -1;
    if (len > failAt - consumed) len = failAt - consumed;
    const int got = MemoryRecordStream::read(buf, len);
    consumed += static_cast<size_t>(got);
    return got;
  }

 private:
  size_t failAt;
  size_t consumed = 0;
};

}  // namespace

TEST(RecordFileTest, DecodingIgnoresBufferAlignmentAndReadSizes) {
  const std::vector<TestEntry> entries = sampleEntries();
  const WrittenTree tree = writeTree(entries);
  for (size_t shift = 1; shift < 8; ++shift) {
    SCOPED_TRACE(shift);
    std::vector<uint8_t> shifted(shift, 0xEE);
    shifted.insert(shifted.end(), tree.bytes.begin(), tree.bytes.end());
    OneByteSource source(shifted.data() + shift, tree.bytes.size());
    std::vector<TestEntry> read;
    EXPECT_EQ(readAll(source, RecordFileKind::Tree, read), RecordStatus::End);
    expectSameEntries(read, entries, entries.size());
  }
}

TEST(RecordFileTest, SourceErrorAtEveryPositionIsIoErrorAndSticks) {
  WrittenTree tree = writeTree(sampleEntries());
  for (size_t failAt = 0; failAt <= tree.bytes.size(); ++failAt) {
    SCOPED_TRACE(failAt);
    FailingSource source(tree.bytes.data(), tree.bytes.size(), failAt);
    RecordReader reader(source, RecordFileKind::Tree);
    SyncRecord record;
    RecordStatus status;
    while ((status = reader.next(record)) == RecordStatus::Record) {
    }
    EXPECT_EQ(status, RecordStatus::IoError);
    EXPECT_EQ(reader.next(record), RecordStatus::IoError);
  }
}

TEST(RecordWriterTest, RefusedCallsWriteNothing) {
  uint8_t file[FILE_CAPACITY];
  MemoryRecordStream stream(file, sizeof(file));
  RecordWriter writer(stream);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  fillSha(sha, 1);
  const std::string tooLong(MAX_SYNC_PATH_BYTES + 1, 'x');

  EXPECT_FALSE(writer.put(sha, 1, "a", 1, NO_OFFSET, nullptr));
  EXPECT_FALSE(writer.seal());
  EXPECT_EQ(stream.length(), 0u);

  ASSERT_TRUE(writer.begin(RecordFileKind::Tree));
  const size_t afterHeader = stream.length();
  EXPECT_FALSE(writer.begin(RecordFileKind::Tree));
  EXPECT_FALSE(writer.put(sha, 1, "", 0, NO_OFFSET, nullptr));
  EXPECT_FALSE(writer.put(sha, 1, nullptr, 1, NO_OFFSET, nullptr));
  EXPECT_FALSE(writer.put(nullptr, 1, "a", 1, NO_OFFSET, nullptr));
  EXPECT_FALSE(writer.put(sha, 1, tooLong.data(), tooLong.size(), NO_OFFSET, nullptr));
  EXPECT_FALSE(writer.put(sha, 1, "a", 1, static_cast<uint32_t>(RECORD_HEADER_SIZE), nullptr));
  EXPECT_EQ(stream.length(), afterHeader);
  EXPECT_FALSE(writer.failed());

  ASSERT_TRUE(writer.put(sha, 1, tooLong.data(), MAX_SYNC_PATH_BYTES, NO_OFFSET, nullptr));
  ASSERT_TRUE(writer.seal());
  const size_t sealed = stream.length();
  EXPECT_FALSE(writer.put(sha, 1, "a", 1, NO_OFFSET, nullptr));
  EXPECT_FALSE(writer.seal());
  EXPECT_FALSE(writer.begin(RecordFileKind::Tree));
  EXPECT_EQ(stream.length(), sealed);
  EXPECT_FALSE(writer.failed());

  std::vector<TestEntry> read;
  std::vector<uint8_t> bytes(file, file + sealed);
  EXPECT_EQ(readAll(bytes, RecordFileKind::Tree, read), RecordStatus::End);
  EXPECT_EQ(read.size(), 1u);
}

namespace {

// Fails its failOn-th write (1-based) and every later one, after writing half of the first failed one.
class FailingSink : public RecordSink {
 public:
  explicit FailingSink(size_t failOn) : failOn(failOn) {}
  bool write(const uint8_t* buf, size_t len) override {
    ++writes;
    if (writes < failOn) {
      bytes.insert(bytes.end(), buf, buf + len);
      return true;
    }
    if (writes == failOn) bytes.insert(bytes.end(), buf, buf + len / 2);
    return false;
  }
  std::vector<uint8_t> bytes;

 private:
  size_t failOn;
  size_t writes = 0;
};

}  // namespace

TEST(RecordWriterTest, FailedSinkWriteIsStickyAndLeavesATruncatedTree) {
  const std::vector<TestEntry> entries = sampleEntries();
  const size_t totalWrites = entries.size() + 2;
  for (size_t failOn = 1; failOn <= totalWrites; ++failOn) {
    SCOPED_TRACE(failOn);
    FailingSink sink(failOn);
    RecordWriter writer(sink);
    size_t succeeded = writer.begin(RecordFileKind::Tree) ? 1 : 0;
    for (const TestEntry& entry : entries) {
      if (writer.put(entry.sha, entry.size, entry.path.data(), entry.path.size(), NO_OFFSET, nullptr)) ++succeeded;
    }
    if (writer.seal()) ++succeeded;
    EXPECT_EQ(succeeded, failOn - 1);
    EXPECT_TRUE(writer.failed());

    std::vector<TestEntry> read;
    EXPECT_EQ(readAll(sink.bytes, RecordFileKind::Tree, read), RecordStatus::Truncated);
  }
}

TEST(MemoryRecordStreamTest, WritePastCapacityFailsAndWritesNothing) {
  uint8_t buffer[10] = {};
  MemoryRecordStream stream(buffer, sizeof(buffer));
  const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_TRUE(stream.write(data, 8));
  EXPECT_FALSE(stream.write(data, 3));
  EXPECT_EQ(stream.length(), 8u);
  EXPECT_TRUE(stream.write(data, 2));
  EXPECT_EQ(stream.length(), 10u);
}

TEST(MemoryRecordStreamTest, ReadsFollowTheirOwnCursorWithinLength) {
  uint8_t buffer[8] = {10, 11, 12, 13, 14, 15, 16, 17};
  MemoryRecordStream stream(buffer, sizeof(buffer), 6);
  uint8_t out[8] = {};
  EXPECT_EQ(stream.read(out, 4), 4);
  EXPECT_EQ(stream.read(out, 4), 2);
  EXPECT_EQ(out[1], 15);
  EXPECT_EQ(stream.read(out, 4), 0);
  EXPECT_TRUE(stream.seek(6));
  EXPECT_FALSE(stream.seek(7));
  EXPECT_TRUE(stream.seek(1));
  EXPECT_EQ(stream.read(out, 1), 1);
  EXPECT_EQ(out[0], 11);
}

namespace {

struct ReadRecord {
  RecordFrameKind kind;
  uint32_t offset;
  bool inJournal;
  std::string path;
  uint64_t size;
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  uint32_t replaces;
  uint32_t target;
};

struct ManifestRead {
  RecordStatus status;
  std::vector<ReadRecord> records;
  bool tornTail;
  uint32_t endOffset;
};

ManifestRead readManifest(RecordSource& source) {
  RecordReader reader(source, RecordFileKind::Manifest);
  ManifestRead result{};
  SyncRecord record;
  while ((result.status = reader.next(record)) == RecordStatus::Record) {
    ReadRecord copy{record.kind, record.offset, record.inJournal, {}, 0, {}, NO_OFFSET, NO_OFFSET};
    if (record.kind == RecordFrameKind::Entry) {
      copy.path.assign(record.path, record.pathLen);
      EXPECT_EQ(strlen(record.path), record.pathLen);
      copy.size = record.size;
      memcpy(copy.sha, record.sha, sizeof(copy.sha));
      copy.replaces = record.replaces;
    } else {
      copy.target = record.target;
    }
    result.records.push_back(copy);
  }
  result.tornTail = reader.tornTail();
  result.endOffset = reader.endOffset();
  return result;
}

ManifestRead readManifest(std::vector<uint8_t>& bytes, size_t length = SIZE_MAX) {
  MemoryRecordStream stream(bytes.data(), bytes.size(), length);
  return readManifest(stream);
}

// A base of three entries, then a journal with an add, a modify, a move, a drop of a base entry
// and a drop of a journal entry.
struct WrittenManifest {
  std::vector<uint8_t> bytes;
  std::vector<ReadRecord> records;
  uint32_t sealEnd;
};

WrittenManifest writeManifest() {
  WrittenManifest manifest;
  manifest.bytes.resize(FILE_CAPACITY);
  MemoryRecordStream stream(manifest.bytes.data(), manifest.bytes.size());
  RecordWriter writer(stream);
  EXPECT_TRUE(writer.begin(RecordFileKind::Manifest));
  auto put = [&](const std::string& path, uint64_t size, uint8_t seed, uint32_t replaces, bool inJournal) {
    ReadRecord record{RecordFrameKind::Entry, 0, inJournal, path, size, {}, replaces, NO_OFFSET};
    fillSha(record.sha, seed);
    EXPECT_TRUE(writer.put(record.sha, size, path.data(), path.size(), replaces, &record.offset));
    manifest.records.push_back(record);
    return record.offset;
  };
  auto drop = [&](uint32_t target) {
    ReadRecord record{
        RecordFrameKind::Drop, static_cast<uint32_t>(stream.length()), true, {}, 0, {}, NO_OFFSET, target};
    EXPECT_TRUE(writer.drop(target));
    manifest.records.push_back(record);
  };
  const uint32_t a = put("a.epub", 10, 1, NO_OFFSET, false);
  const uint32_t b = put("Livres/\xC3\x89t\xC3\xA9.epub", 20, 2, NO_OFFSET, false);
  const uint32_t c = put("c.epub", 30, 3, NO_OFFSET, false);
  EXPECT_TRUE(writer.seal());
  manifest.sealEnd = static_cast<uint32_t>(stream.length());
  const uint32_t added = put("new.epub", 40, 4, NO_OFFSET, true);
  put("a.epub", 11, 5, a, true);
  put("moved/b.epub", 20, 2, b, true);
  drop(c);
  drop(added);
  manifest.bytes.resize(stream.length());
  return manifest;
}

void expectSameRecords(const std::vector<ReadRecord>& actual, const std::vector<ReadRecord>& expected, size_t count) {
  ASSERT_EQ(actual.size(), count);
  for (size_t i = 0; i < count; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(actual[i].kind, expected[i].kind);
    EXPECT_EQ(actual[i].offset, expected[i].offset);
    EXPECT_EQ(actual[i].inJournal, expected[i].inJournal);
    EXPECT_EQ(actual[i].path, expected[i].path);
    EXPECT_EQ(actual[i].size, expected[i].size);
    EXPECT_EQ(memcmp(actual[i].sha, expected[i].sha, GitBlobHasher::SHA_SIZE), 0);
    EXPECT_EQ(actual[i].replaces, expected[i].replaces);
    EXPECT_EQ(actual[i].target, expected[i].target);
  }
}

}  // namespace

TEST(ManifestFileTest, BaseAndJournalRoundTrip) {
  WrittenManifest manifest = writeManifest();
  const ManifestRead read = readManifest(manifest.bytes);
  EXPECT_EQ(read.status, RecordStatus::End);
  expectSameRecords(read.records, manifest.records, manifest.records.size());
  EXPECT_FALSE(read.tornTail);
  EXPECT_EQ(read.endOffset, manifest.bytes.size());
}

namespace {

// A hand-built manifest with one base entry at offset 8; journal frames follow its seal.
std::vector<uint8_t> handBuiltBase() {
  std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Manifest);
  appendFrame(bytes, 'P', entryBody("a.epub"));
  appendFrame(bytes, 'S', u32Body(1));
  return bytes;
}

}  // namespace

TEST(ManifestFileTest, HandBuiltJournalTargetingEarlierFramesReadsEnd) {
  std::vector<uint8_t> bytes = handBuiltBase();
  const uint32_t journalEntry = static_cast<uint32_t>(bytes.size());
  appendFrame(bytes, 'P', entryBody("b.epub", RECORD_HEADER_SIZE));
  appendFrame(bytes, 'D', u32Body(journalEntry));
  EXPECT_EQ(readManifest(bytes).status, RecordStatus::End);
}

TEST(ManifestFileTest, JournalTargetNotBelowItsOwnOffsetOrBelowHeaderIsCorrupt) {
  for (uint32_t target : {0u, 7u}) {
    std::vector<uint8_t> drop = handBuiltBase();
    appendFrame(drop, 'D', u32Body(target));
    EXPECT_EQ(readManifest(drop).status, RecordStatus::Corrupt) << target;
    std::vector<uint8_t> replace = handBuiltBase();
    appendFrame(replace, 'P', entryBody("b.epub", target));
    EXPECT_EQ(readManifest(replace).status, RecordStatus::Corrupt) << target;
  }
  for (uint32_t delta : {0u, 1u, 1000u}) {
    std::vector<uint8_t> drop = handBuiltBase();
    appendFrame(drop, 'D', u32Body(static_cast<uint32_t>(drop.size()) + delta));
    EXPECT_EQ(readManifest(drop).status, RecordStatus::Corrupt) << delta;
    std::vector<uint8_t> replace = handBuiltBase();
    appendFrame(replace, 'P', entryBody("b.epub", static_cast<uint32_t>(replace.size()) + delta));
    EXPECT_EQ(readManifest(replace).status, RecordStatus::Corrupt) << delta;
  }
}

TEST(ManifestFileTest, SealInJournalIsCorrupt) {
  std::vector<uint8_t> bytes = handBuiltBase();
  appendFrame(bytes, 'S', u32Body(1));
  EXPECT_EQ(readManifest(bytes).status, RecordStatus::Corrupt);
}

TEST(ManifestFileTest, DropOrReplacesInBaseIsCorrupt) {
  std::vector<uint8_t> drop = fileHeader(RecordFileKind::Manifest);
  appendFrame(drop, 'P', entryBody("a.epub"));
  appendFrame(drop, 'D', u32Body(RECORD_HEADER_SIZE));
  appendFrame(drop, 'S', u32Body(1));
  EXPECT_EQ(readManifest(drop).status, RecordStatus::Corrupt);

  std::vector<uint8_t> replace = fileHeader(RecordFileKind::Manifest);
  appendFrame(replace, 'P', entryBody("a.epub"));
  appendFrame(replace, 'P', entryBody("b.epub", RECORD_HEADER_SIZE));
  appendFrame(replace, 'S', u32Body(2));
  EXPECT_EQ(readManifest(replace).status, RecordStatus::Corrupt);
}

TEST(ManifestFileTest, BaseSealCountMismatchIsCorrupt) {
  std::vector<uint8_t> bytes = fileHeader(RecordFileKind::Manifest);
  appendFrame(bytes, 'P', entryBody("a.epub"));
  appendFrame(bytes, 'S', u32Body(2));
  EXPECT_EQ(readManifest(bytes).status, RecordStatus::Corrupt);
}

TEST(ManifestFileTest, CutAtEveryByteOffsetKeepsExactlyTheCompleteJournalFrames) {
  WrittenManifest manifest = writeManifest();
  size_t baseCount = 0;
  while (!manifest.records[baseCount].inJournal) ++baseCount;
  for (size_t k = 0; k < manifest.bytes.size(); ++k) {
    SCOPED_TRACE(k);
    const ManifestRead read = readManifest(manifest.bytes, k);
    if (k < manifest.sealEnd) {
      EXPECT_EQ(read.status, RecordStatus::Truncated);
      continue;
    }
    ASSERT_EQ(read.status, RecordStatus::End);
    size_t complete = baseCount;
    uint32_t validEnd = manifest.sealEnd;
    while (complete < manifest.records.size()) {
      const uint32_t frameEnd = complete + 1 < manifest.records.size() ? manifest.records[complete + 1].offset
                                                                       : static_cast<uint32_t>(manifest.bytes.size());
      if (frameEnd > k) break;
      validEnd = frameEnd;
      ++complete;
    }
    expectSameRecords(read.records, manifest.records, complete);
    EXPECT_EQ(read.tornTail, validEnd != k);
    EXPECT_EQ(read.endOffset, validEnd);
  }
}

namespace {

// The frame holding byte i, and whether i lies in that frame's CRC or body, which the CRC covers alone.
struct FramePosition {
  size_t index;
  bool crcOrBody;
};

FramePosition framePosition(const WrittenManifest& manifest, size_t i) {
  size_t index = 0;
  while (index + 1 < manifest.records.size() && manifest.records[index + 1].offset <= i) ++index;
  return {index, i >= manifest.records[index].offset + 3};
}

}  // namespace

TEST(ManifestFileTest, FlippedBitNeverReadsEndWithAlteredRecords) {
  const WrittenManifest manifest = writeManifest();
  const size_t last = manifest.records.size() - 1;
  for (size_t i = 0; i < manifest.bytes.size(); ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      SCOPED_TRACE(testing::Message() << "byte " << i << " bit " << bit);
      std::vector<uint8_t> bytes = manifest.bytes;
      bytes[i] ^= static_cast<uint8_t>(1u << bit);
      const ManifestRead read = readManifest(bytes);
      if (i < manifest.sealEnd) {
        EXPECT_NE(read.status, RecordStatus::End);
        continue;
      }
      ASSERT_TRUE(read.status == RecordStatus::End || read.status == RecordStatus::Corrupt);
      if (read.status == RecordStatus::End) {
        ASSERT_LT(read.records.size(), manifest.records.size());
        expectSameRecords(read.records, manifest.records, read.records.size());
        EXPECT_TRUE(read.tornTail);
      }
      const FramePosition at = framePosition(manifest, i);
      if (!at.crcOrBody) continue;
      if (at.index == last) {
        EXPECT_EQ(read.status, RecordStatus::End);
        expectSameRecords(read.records, manifest.records, last);
        EXPECT_EQ(read.endOffset, manifest.records[last].offset);
      } else {
        EXPECT_EQ(read.status, RecordStatus::Corrupt);
      }
    }
  }
}

TEST(ManifestFileTest, FlippedBitInBaseFrameIsCorrupt) {
  const WrittenManifest manifest = writeManifest();
  for (size_t i = RECORD_HEADER_SIZE; i < manifest.sealEnd; ++i) {
    std::vector<uint8_t> bytes = manifest.bytes;
    bytes[i] ^= 0x01;
    EXPECT_EQ(readManifest(bytes).status, RecordStatus::Corrupt) << i;
  }
}

TEST(ManifestFileTest, AppendToResumesTheJournalAtTheReadEndOffset) {
  uint8_t file[FILE_CAPACITY];
  MemoryRecordStream stream(file, sizeof(file));
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  fillSha(sha, 9);
  uint32_t base = 0;
  {
    RecordWriter writer(stream);
    ASSERT_TRUE(writer.begin(RecordFileKind::Manifest));
    ASSERT_TRUE(writer.put(sha, 5, "a.epub", 6, NO_OFFSET, &base));
    ASSERT_TRUE(writer.seal());
  }
  const ManifestRead before = readManifest(stream);
  ASSERT_EQ(before.status, RecordStatus::End);
  ASSERT_EQ(before.endOffset, stream.length());

  RecordWriter writer(stream);
  ASSERT_TRUE(writer.appendTo(before.endOffset));
  uint32_t moved = 0;
  ASSERT_TRUE(writer.put(sha, 5, "b.epub", 6, base, &moved));
  EXPECT_EQ(moved, before.endOffset);
  ASSERT_TRUE(writer.drop(moved));

  ASSERT_TRUE(stream.seek(0));
  const ManifestRead after = readManifest(stream);
  EXPECT_EQ(after.status, RecordStatus::End);
  ASSERT_EQ(after.records.size(), 3u);
  EXPECT_EQ(after.records[1].offset, moved);
  EXPECT_EQ(after.records[1].replaces, base);
  EXPECT_EQ(after.records[1].path, "b.epub");
  EXPECT_EQ(after.records[2].target, moved);
  EXPECT_TRUE(after.records[2].inJournal);
}

TEST(RecordWriterTest, RefusesOutOfGrammarManifestFramesAndWritesNothing) {
  uint8_t file[FILE_CAPACITY];
  MemoryRecordStream stream(file, sizeof(file));
  RecordWriter writer(stream);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  fillSha(sha, 3);
  EXPECT_FALSE(writer.drop(RECORD_HEADER_SIZE));
  ASSERT_TRUE(writer.begin(RecordFileKind::Manifest));
  EXPECT_FALSE(writer.appendTo(RECORD_HEADER_SIZE));
  uint32_t base = 0;
  ASSERT_TRUE(writer.put(sha, 1, "a", 1, NO_OFFSET, &base));
  size_t length = stream.length();
  EXPECT_FALSE(writer.put(sha, 1, "b", 1, base, nullptr));
  EXPECT_FALSE(writer.drop(base));
  EXPECT_EQ(stream.length(), length);

  ASSERT_TRUE(writer.seal());
  length = stream.length();
  const auto position = static_cast<uint32_t>(length);
  EXPECT_FALSE(writer.seal());
  for (uint32_t target : {0u, 7u, position, position + 1, NO_OFFSET - 1}) {
    EXPECT_FALSE(writer.drop(target)) << target;
    EXPECT_FALSE(writer.put(sha, 1, "b", 1, target, nullptr)) << target;
  }
  EXPECT_EQ(stream.length(), length);
  EXPECT_FALSE(writer.failed());

  ASSERT_TRUE(writer.drop(base));
  EXPECT_EQ(readManifest(stream).status, RecordStatus::End);
}

TEST(RecordWriterTest, DropInTreeIsRefused) {
  uint8_t file[FILE_CAPACITY];
  MemoryRecordStream stream(file, sizeof(file));
  RecordWriter writer(stream);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  fillSha(sha, 3);
  ASSERT_TRUE(writer.begin(RecordFileKind::Tree));
  uint32_t entry = 0;
  ASSERT_TRUE(writer.put(sha, 1, "a", 1, NO_OFFSET, &entry));
  EXPECT_FALSE(writer.drop(entry));
  ASSERT_TRUE(writer.seal());
  EXPECT_FALSE(writer.drop(entry));
  EXPECT_FALSE(writer.put(sha, 1, "b", 1, entry, nullptr));
}

TEST(RecordWriterTest, AppendToRefusesAnOffsetBeforeAnEmptyManifestsEnd) {
  uint8_t file[FILE_CAPACITY];
  MemoryRecordStream stream(file, sizeof(file));
  const auto emptyManifest = static_cast<uint32_t>(RECORD_HEADER_SIZE + FRAME_HEADER_SIZE + OFFSET_BODY_SIZE);
  for (uint32_t endOffset : {0u, 8u, emptyManifest - 1}) {
    RecordWriter writer(stream);
    EXPECT_FALSE(writer.appendTo(endOffset)) << endOffset;
  }
  RecordWriter writer(stream);
  EXPECT_TRUE(writer.appendTo(emptyManifest));
  EXPECT_FALSE(writer.appendTo(emptyManifest));
  EXPECT_FALSE(writer.begin(RecordFileKind::Manifest));
  EXPECT_FALSE(writer.seal());
  EXPECT_EQ(stream.length(), 0u);
}

TEST(RecordWriterTest, FailedSinkWriteLeavesATruncatedBaseOrATornJournal) {
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  fillSha(sha, 4);
  constexpr size_t BASE_WRITES = 3;  // header, one entry, seal
  constexpr size_t TOTAL_WRITES = BASE_WRITES + 3;
  for (size_t failOn = 1; failOn <= TOTAL_WRITES; ++failOn) {
    SCOPED_TRACE(failOn);
    FailingSink sink(failOn);
    RecordWriter writer(sink);
    uint32_t base = RECORD_HEADER_SIZE;
    uint32_t added = 0;
    size_t succeeded = writer.begin(RecordFileKind::Manifest) ? 1 : 0;
    if (writer.put(sha, 1, "a.epub", 6, NO_OFFSET, &base)) ++succeeded;
    if (writer.seal()) ++succeeded;
    if (writer.put(sha, 2, "b.epub", 6, NO_OFFSET, &added)) ++succeeded;
    if (writer.put(sha, 3, "c.epub", 6, base, nullptr)) ++succeeded;
    if (writer.drop(added == 0 ? base : added)) ++succeeded;
    EXPECT_EQ(succeeded, failOn - 1);
    EXPECT_TRUE(writer.failed());

    const ManifestRead read = readManifest(sink.bytes);
    if (failOn <= BASE_WRITES) {
      EXPECT_EQ(read.status, RecordStatus::Truncated);
    } else {
      EXPECT_EQ(read.status, RecordStatus::End);
      EXPECT_TRUE(read.tornTail);
      EXPECT_EQ(read.records.size(), failOn - 3);
    }
  }
}

namespace {

// Reads each listed entry back by offset with one reader, in the order given.
void expectEntriesReadBack(std::vector<uint8_t>& bytes, RecordFileKind kind, const std::vector<ReadRecord>& expected,
                           const std::vector<size_t>& order) {
  MemoryRecordStream stream(bytes.data(), bytes.size(), bytes.size());
  RecordReader reader(stream, kind);
  for (size_t i : order) {
    SCOPED_TRACE(i);
    SyncRecord record;
    ASSERT_EQ(reader.readEntryAt(expected[i].offset, record), RecordStatus::Record);
    EXPECT_EQ(record.kind, RecordFrameKind::Entry);
    EXPECT_EQ(record.offset, expected[i].offset);
    EXPECT_EQ(std::string(record.path), expected[i].path);
    EXPECT_EQ(record.pathLen, expected[i].path.size());
    EXPECT_EQ(record.size, expected[i].size);
    EXPECT_EQ(memcmp(record.sha, expected[i].sha, GitBlobHasher::SHA_SIZE), 0);
    EXPECT_EQ(record.replaces, expected[i].replaces);
  }
}

// Forward, reverse and a fixed shuffle of 0..count-1.
std::vector<std::vector<size_t>> readBackOrders(size_t count) {
  std::vector<size_t> forward;
  for (size_t i = 0; i < count; ++i) forward.push_back(i);
  std::vector<size_t> reverse(forward.rbegin(), forward.rend());
  std::vector<size_t> shuffled = forward;
  uint32_t seed = 12345;
  for (size_t i = shuffled.size(); i > 1; --i) {
    seed = seed * 1103515245u + 12345u;
    std::swap(shuffled[i - 1], shuffled[(seed >> 8) % i]);
  }
  return {forward, reverse, shuffled};
}

}  // namespace

TEST(ReadEntryAtTest, ReadsBackEveryTreeEntryInAnyOrder) {
  const std::vector<TestEntry> entries = sampleEntries();
  WrittenTree tree = writeTree(entries);
  std::vector<ReadRecord> expected;
  for (size_t i = 0; i < entries.size(); ++i) {
    ReadRecord record{
        RecordFrameKind::Entry, tree.offsets[i], false, entries[i].path, entries[i].size, {}, NO_OFFSET, NO_OFFSET};
    memcpy(record.sha, entries[i].sha, sizeof(record.sha));
    expected.push_back(record);
  }
  for (const std::vector<size_t>& order : readBackOrders(expected.size())) {
    expectEntriesReadBack(tree.bytes, RecordFileKind::Tree, expected, order);
  }
}

TEST(ReadEntryAtTest, ReadsBackEveryManifestEntryInBaseAndJournal) {
  WrittenManifest manifest = writeManifest();
  std::vector<ReadRecord> entries;
  for (const ReadRecord& record : manifest.records) {
    if (record.kind == RecordFrameKind::Entry) entries.push_back(record);
  }
  for (const std::vector<size_t>& order : readBackOrders(entries.size())) {
    expectEntriesReadBack(manifest.bytes, RecordFileKind::Manifest, entries, order);
  }
}

namespace {

RecordStatus readEntryAt(std::vector<uint8_t>& bytes, uint32_t offset, size_t length = SIZE_MAX) {
  MemoryRecordStream stream(bytes.data(), bytes.size(), length);
  RecordReader reader(stream, RecordFileKind::Manifest);
  SyncRecord record;
  return reader.readEntryAt(offset, record);
}

}  // namespace

TEST(ReadEntryAtTest, OffsetThatIsNotAnEntryFrameIsRefused) {
  WrittenManifest manifest = writeManifest();
  EXPECT_EQ(readEntryAt(manifest.bytes, 0), RecordStatus::Corrupt);
  EXPECT_EQ(readEntryAt(manifest.bytes, RECORD_HEADER_SIZE - 1), RecordStatus::Corrupt);
  const uint32_t seal = manifest.sealEnd - FRAME_HEADER_SIZE - OFFSET_BODY_SIZE;
  EXPECT_EQ(readEntryAt(manifest.bytes, seal), RecordStatus::Corrupt);
  for (const ReadRecord& record : manifest.records) {
    SCOPED_TRACE(record.offset);
    if (record.kind == RecordFrameKind::Drop) {
      EXPECT_EQ(readEntryAt(manifest.bytes, record.offset), RecordStatus::Corrupt);
    } else {
      for (uint32_t inside = 1; inside < FRAME_HEADER_SIZE + ENTRY_FIXED_BODY_SIZE; ++inside) {
        EXPECT_NE(readEntryAt(manifest.bytes, record.offset + inside), RecordStatus::Record) << inside;
      }
    }
  }
}

TEST(ReadEntryAtTest, DamagedOrCutEntryFrameIsRefused) {
  const WrittenManifest manifest = writeManifest();
  for (const ReadRecord& record : manifest.records) {
    if (record.kind != RecordFrameKind::Entry) continue;
    SCOPED_TRACE(record.offset);
    const size_t frameEnd = record.offset + FRAME_HEADER_SIZE + ENTRY_FIXED_BODY_SIZE + record.path.size();
    for (size_t i = record.offset + 3; i < frameEnd; ++i) {
      std::vector<uint8_t> bytes = manifest.bytes;
      bytes[i] ^= 0x40;
      EXPECT_EQ(readEntryAt(bytes, record.offset), RecordStatus::Corrupt) << i;
    }
    for (size_t k = record.offset; k < frameEnd; ++k) {
      std::vector<uint8_t> bytes = manifest.bytes;
      EXPECT_EQ(readEntryAt(bytes, record.offset, k), RecordStatus::Truncated) << k;
    }
  }
}

TEST(ReadEntryAtTest, ReplacesNotBelowTheFrameIsCorrupt) {
  std::vector<uint8_t> bytes = handBuiltBase();
  const auto offset = static_cast<uint32_t>(bytes.size());
  appendFrame(bytes, 'P', entryBody("b.epub", offset));
  EXPECT_EQ(readEntryAt(bytes, offset), RecordStatus::Corrupt);
}

TEST(ReadEntryAtTest, RefusedSeekOrReadIsIoError) {
  WrittenManifest manifest = writeManifest();
  EXPECT_EQ(readEntryAt(manifest.bytes, static_cast<uint32_t>(manifest.bytes.size()) + 1), RecordStatus::IoError);
  for (const ReadRecord& record : manifest.records) {
    if (record.kind != RecordFrameKind::Entry) continue;
    FailingSource source(manifest.bytes.data(), manifest.bytes.size(), 3);
    RecordReader reader(source, RecordFileKind::Manifest);
    SyncRecord read;
    EXPECT_EQ(reader.readEntryAt(record.offset, read), RecordStatus::IoError) << record.offset;
  }
}

TEST(ManifestFileTest, EachJournalOperationAppendsOnlyItsOwnFrame) {
  constexpr size_t COUNT = 200;
  std::vector<uint8_t> bytes(65536);
  MemoryRecordStream stream(bytes.data(), bytes.size());
  RecordWriter writer(stream);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  fillSha(sha, 6);
  std::vector<uint32_t> offsets;
  offsets.reserve(COUNT);
  ASSERT_TRUE(writer.begin(RecordFileKind::Manifest));
  for (size_t i = 0; i < COUNT; ++i) {
    const std::string path = "books/" + std::to_string(i) + ".epub";
    uint32_t offset = 0;
    ASSERT_TRUE(writer.put(sha, i, path.data(), path.size(), NO_OFFSET, &offset));
    offsets.push_back(offset);
  }
  ASSERT_TRUE(writer.seal());

  size_t expected = stream.length();
  for (size_t i = 0; i < COUNT; ++i) {
    if (i % 2 == 0) {
      const std::string path = "moved/" + std::to_string(i) + ".epub";
      ASSERT_TRUE(writer.put(sha, i, path.data(), path.size(), offsets[i], nullptr));
      expected += FRAME_HEADER_SIZE + ENTRY_FIXED_BODY_SIZE + path.size();
    } else {
      ASSERT_TRUE(writer.drop(offsets[i]));
      expected += FRAME_HEADER_SIZE + OFFSET_BODY_SIZE;
    }
    ASSERT_EQ(stream.length(), expected) << i;
  }
  EXPECT_EQ(readManifest(stream).records.size(), 2 * COUNT);
}

TEST(ManifestFileTest, SourceErrorAtEveryPositionIsIoErrorNotATornTail) {
  WrittenManifest manifest = writeManifest();
  for (size_t failAt = 0; failAt <= manifest.bytes.size(); ++failAt) {
    SCOPED_TRACE(failAt);
    FailingSource source(manifest.bytes.data(), manifest.bytes.size(), failAt);
    EXPECT_EQ(readManifest(source).status, RecordStatus::IoError);
  }
}

TEST(ManifestFileTest, TornEndRepeatsOnEveryLaterCall) {
  WrittenManifest manifest = writeManifest();
  MemoryRecordStream stream(manifest.bytes.data(), manifest.bytes.size(), manifest.bytes.size() - 1);
  RecordReader reader(stream, RecordFileKind::Manifest);
  SyncRecord record;
  while (reader.next(record) == RecordStatus::Record) {
  }
  EXPECT_EQ(reader.next(record), RecordStatus::End);
  EXPECT_TRUE(reader.tornTail());
  EXPECT_EQ(reader.endOffset(), manifest.records.back().offset);
}

namespace {

struct Compacted {
  CompactResult result;
  std::vector<uint8_t> bytes;
};

Compacted compact(std::vector<uint8_t>& input, uint32_t* scratch, size_t capacity, size_t length = SIZE_MAX) {
  MemoryRecordStream source(input.data(), input.size(), length);
  Compacted out{{}, std::vector<uint8_t>(FILE_CAPACITY)};
  MemoryRecordStream sink(out.bytes.data(), out.bytes.size());
  ManifestCompactor compactor(source, sink);
  out.result = compactor.compact(scratch, capacity);
  out.bytes.resize(sink.length());
  return out;
}

std::vector<ReadRecord> baseEntries(const std::vector<ReadRecord>& records) {
  std::vector<ReadRecord> base;
  for (const ReadRecord& record : records) {
    if (!record.inJournal) base.push_back(record);
  }
  return base;
}

// Compares path, size and SHA only: offsets move and replaces is cleared by compaction.
void expectSameLiveEntries(const std::vector<ReadRecord>& actual, const std::vector<ReadRecord>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < actual.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(actual[i].kind, RecordFrameKind::Entry);
    EXPECT_FALSE(actual[i].inJournal);
    EXPECT_EQ(actual[i].replaces, NO_OFFSET);
    EXPECT_EQ(actual[i].path, expected[i].path);
    EXPECT_EQ(actual[i].size, expected[i].size);
    EXPECT_EQ(memcmp(actual[i].sha, expected[i].sha, GitBlobHasher::SHA_SIZE), 0);
  }
}

}  // namespace

TEST(ManifestCompactorTest, BaseWithoutJournalKeepsEveryEntry) {
  WrittenManifest manifest = writeManifest();
  manifest.bytes.resize(manifest.sealEnd);
  const std::vector<ReadRecord> base = baseEntries(manifest.records);

  Compacted out = compact(manifest.bytes, nullptr, 0);

  EXPECT_EQ(out.result.status, CompactStatus::Ok);
  EXPECT_EQ(out.result.liveCount, base.size());
  EXPECT_FALSE(out.result.tornTailDropped);
  const ManifestRead read = readManifest(out.bytes);
  EXPECT_EQ(read.status, RecordStatus::End);
  expectSameLiveEntries(read.records, base);
}

// writeManifest's journal leaves the modified a.epub and the moved b.epub, in file order.
TEST(ManifestCompactorTest, JournalFoldsIntoTheLiveEntries) {
  WrittenManifest manifest = writeManifest();
  const std::vector<ReadRecord> live{manifest.records[4], manifest.records[5]};
  uint32_t scratch[4];

  Compacted out = compact(manifest.bytes, scratch, 4);

  EXPECT_EQ(out.result.status, CompactStatus::Ok);
  EXPECT_EQ(out.result.liveCount, 2u);
  EXPECT_FALSE(out.result.tornTailDropped);
  const ManifestRead read = readManifest(out.bytes);
  EXPECT_EQ(read.status, RecordStatus::End);
  EXPECT_EQ(read.endOffset, out.bytes.size());
  expectSameLiveEntries(read.records, live);
}

TEST(ManifestCompactorTest, CompactingTheOutputAgainGivesIdenticalBytes) {
  WrittenManifest manifest = writeManifest();
  uint32_t scratch[4];
  Compacted once = compact(manifest.bytes, scratch, 4);
  ASSERT_EQ(once.result.status, CompactStatus::Ok);

  Compacted twice = compact(once.bytes, nullptr, 0);

  EXPECT_EQ(twice.result.status, CompactStatus::Ok);
  EXPECT_EQ(twice.result.liveCount, once.result.liveCount);
  EXPECT_EQ(twice.bytes, once.bytes);
}

TEST(ManifestCompactorTest, ScratchOneSmallerThanTheTargetsIsScratchFull) {
  WrittenManifest manifest = writeManifest();
  uint32_t scratch[4] = {};

  EXPECT_EQ(compact(manifest.bytes, scratch, 3).result.status, CompactStatus::ScratchFull);
  EXPECT_EQ(compact(manifest.bytes, nullptr, 0).result.status, CompactStatus::ScratchFull);
  EXPECT_EQ(compact(manifest.bytes, nullptr, 4).result.status, CompactStatus::ScratchFull);
}

TEST(ManifestCompactorTest, EntryDroppedOrReplacedTwiceIsCorrupt) {
  const uint32_t a = RECORD_HEADER_SIZE;
  std::vector<uint8_t> twiceDropped = handBuiltBase();
  appendFrame(twiceDropped, 'D', u32Body(a));
  appendFrame(twiceDropped, 'D', u32Body(a));
  std::vector<uint8_t> replacedThenDropped = handBuiltBase();
  appendFrame(replacedThenDropped, 'P', entryBody("b.epub", a));
  appendFrame(replacedThenDropped, 'D', u32Body(a));
  std::vector<uint8_t> twiceReplaced = handBuiltBase();
  appendFrame(twiceReplaced, 'P', entryBody("b.epub", a));
  appendFrame(twiceReplaced, 'P', entryBody("c.epub", a));
  uint32_t scratch[2];

  EXPECT_EQ(compact(twiceDropped, scratch, 2).result.status, CompactStatus::Corrupt);
  EXPECT_EQ(compact(replacedThenDropped, scratch, 2).result.status, CompactStatus::Corrupt);
  EXPECT_EQ(compact(twiceReplaced, scratch, 2).result.status, CompactStatus::Corrupt);
}

TEST(ManifestCompactorTest, TargetThatIsNotAnEntryFrameIsCorrupt) {
  const uint32_t a = RECORD_HEADER_SIZE;
  const auto journal = static_cast<uint32_t>(handBuiltBase().size());
  const uint32_t seal = journal - FRAME_HEADER_SIZE - OFFSET_BODY_SIZE;
  std::vector<uint8_t> sealDropped = handBuiltBase();
  appendFrame(sealDropped, 'D', u32Body(seal));
  std::vector<uint8_t> sealReplaced = handBuiltBase();
  appendFrame(sealReplaced, 'P', entryBody("b.epub", seal));
  std::vector<uint8_t> insideEntryDropped = handBuiltBase();
  appendFrame(insideEntryDropped, 'D', u32Body(a + 1));
  std::vector<uint8_t> dropDropped = handBuiltBase();
  appendFrame(dropDropped, 'D', u32Body(a));
  appendFrame(dropDropped, 'D', u32Body(journal));
  uint32_t scratch[2];

  EXPECT_EQ(compact(sealDropped, scratch, 2).result.status, CompactStatus::Corrupt);
  EXPECT_EQ(compact(sealReplaced, scratch, 2).result.status, CompactStatus::Corrupt);
  EXPECT_EQ(compact(insideEntryDropped, scratch, 2).result.status, CompactStatus::Corrupt);
  EXPECT_EQ(compact(dropDropped, scratch, 2).result.status, CompactStatus::Corrupt);
}

// Cutting the last drop off keeps the journal's new.epub alive.
TEST(ManifestCompactorTest, TornTailIsDroppedAndReported) {
  WrittenManifest manifest = writeManifest();
  const std::vector<ReadRecord> live{manifest.records[3], manifest.records[4], manifest.records[5]};
  uint32_t scratch[4];

  Compacted out = compact(manifest.bytes, scratch, 4, manifest.bytes.size() - 1);

  EXPECT_EQ(out.result.status, CompactStatus::Ok);
  EXPECT_TRUE(out.result.tornTailDropped);
  EXPECT_EQ(out.result.liveCount, 3u);
  const ManifestRead read = readManifest(out.bytes);
  EXPECT_EQ(read.status, RecordStatus::End);
  EXPECT_FALSE(read.tornTail);
  expectSameLiveEntries(read.records, live);
}

TEST(ManifestCompactorTest, TruncatedBaseWrongVersionOrSourceErrorIsPassedThrough) {
  WrittenManifest manifest = writeManifest();
  uint32_t scratch[4];
  EXPECT_EQ(compact(manifest.bytes, scratch, 4, manifest.sealEnd - 1).result.status, CompactStatus::Truncated);

  std::vector<uint8_t> otherVersion = manifest.bytes;
  otherVersion[4] = RECORD_FILE_VERSION + 1;
  EXPECT_EQ(compact(otherVersion, scratch, 4).result.status, CompactStatus::WrongVersion);

  std::vector<uint8_t> flipped = manifest.bytes;
  flipped[RECORD_HEADER_SIZE + FRAME_HEADER_SIZE] ^= 0x01;
  EXPECT_EQ(compact(flipped, scratch, 4).result.status, CompactStatus::Corrupt);

  // The second pass reads the file again, so errors up to twice its size reach it.
  std::vector<uint8_t> out(FILE_CAPACITY);
  for (size_t failAt = 0; failAt <= 2 * manifest.bytes.size(); ++failAt) {
    SCOPED_TRACE(failAt);
    FailingSource source(manifest.bytes.data(), manifest.bytes.size(), failAt);
    MemoryRecordStream sink(out.data(), out.size());
    ManifestCompactor compactor(source, sink);
    EXPECT_EQ(compactor.compact(scratch, 4).status, CompactStatus::IoError);
  }
}

namespace {

class UnseekableSource : public MemoryRecordStream {
 public:
  using MemoryRecordStream::MemoryRecordStream;
  bool seek(uint32_t) override { return false; }
};

}  // namespace

TEST(ManifestCompactorTest, RefusedRewindIsIoError) {
  WrittenManifest manifest = writeManifest();
  UnseekableSource source(manifest.bytes.data(), manifest.bytes.size(), manifest.bytes.size());
  std::vector<uint8_t> out(FILE_CAPACITY);
  MemoryRecordStream sink(out.data(), out.size());
  ManifestCompactor compactor(source, sink);
  uint32_t scratch[4];
  EXPECT_EQ(compactor.compact(scratch, 4).status, CompactStatus::IoError);
}

// Header, the two live entries, then the seal: four writes.
TEST(ManifestCompactorTest, FailingSinkIsWriteFailed) {
  WrittenManifest manifest = writeManifest();
  uint32_t scratch[4];
  for (size_t failOn = 1; failOn <= 4; ++failOn) {
    SCOPED_TRACE(failOn);
    MemoryRecordStream source(manifest.bytes.data(), manifest.bytes.size(), manifest.bytes.size());
    FailingSink sink(failOn);
    ManifestCompactor compactor(source, sink);
    EXPECT_EQ(compactor.compact(scratch, 4).status, CompactStatus::WriteFailed);
  }
  MemoryRecordStream source(manifest.bytes.data(), manifest.bytes.size(), manifest.bytes.size());
  FailingSink sink(5);
  ManifestCompactor compactor(source, sink);
  EXPECT_EQ(compactor.compact(scratch, 4).status, CompactStatus::Ok);
}
