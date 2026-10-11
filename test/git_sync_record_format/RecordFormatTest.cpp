#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Crc32.h"
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
