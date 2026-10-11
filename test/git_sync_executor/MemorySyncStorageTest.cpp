#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "MemorySyncStorage.h"

namespace {

bool writeFile(SyncStorage& storage, const char* path, const std::string& content) {
  SyncFile* file = storage.open(path, SyncOpenMode::Truncate);
  if (!file) return false;
  const bool ok = file->write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  storage.close(file);
  return ok;
}

std::string readAll(SyncFile& file) {
  std::string out;
  uint8_t buf[7];
  int n;
  while ((n = file.read(buf, sizeof(buf))) > 0) out.append(reinterpret_cast<const char*>(buf), n);
  return out;
}

}  // namespace

TEST(MemorySyncStorage, WrittenFileReadsBack) {
  MemorySyncStorage storage;
  ASSERT_TRUE(writeFile(storage, "/a.txt", "hello world"));

  SyncFile* file = storage.open("/a.txt", SyncOpenMode::Read);
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(file->size(), 11u);
  EXPECT_EQ(readAll(*file), "hello world");
  storage.close(file);
}

TEST(MemorySyncStorage, TruncateNeedsAnExistingParentThatMkdirsCreates) {
  MemorySyncStorage storage;
  EXPECT_FALSE(writeFile(storage, "/Livres/SF/a.epub", "x"));
  EXPECT_FALSE(storage.exists("/Livres"));

  ASSERT_TRUE(storage.mkdirs("/Livres/SF"));
  EXPECT_TRUE(storage.exists("/Livres"));
  EXPECT_TRUE(storage.exists("/Livres/SF"));
  EXPECT_TRUE(storage.mkdirs("/Livres/SF"));
  EXPECT_TRUE(writeFile(storage, "/Livres/SF/a.epub", "x"));
  EXPECT_TRUE(storage.exists("/Livres/SF/a.epub"));
}

TEST(MemorySyncStorage, MkdirsFailsThroughAFile) {
  MemorySyncStorage storage;
  ASSERT_TRUE(writeFile(storage, "/a", "x"));
  EXPECT_FALSE(storage.mkdirs("/a"));
  EXPECT_FALSE(storage.mkdirs("/a/b"));
}

TEST(MemorySyncStorage, LookupIgnoresCaseAndKeepsTheStoredCase) {
  MemorySyncStorage storage;
  ASSERT_TRUE(storage.mkdirs("/Livres/Été"));
  ASSERT_TRUE(writeFile(storage, "/livres/ÉTÉ/Dune.epub", "abc"));

  EXPECT_TRUE(storage.exists("/LIVRES/été/dune.EPUB"));
  uint64_t size = 0;
  EXPECT_TRUE(storage.fileSize("/livres/été/DUNE.epub", size));
  EXPECT_EQ(size, 3u);

  const MemorySyncStorage::Snapshot snap = storage.snapshot();
  EXPECT_EQ(snap.files.count("/Livres/Été/Dune.epub"), 1u);
  EXPECT_EQ(snap.directories.count("/Livres/Été"), 1u);
}

TEST(MemorySyncStorage, FatForbiddenNamesAreNeverCreated) {
  MemorySyncStorage storage;
  for (const char* name : {"/a:b", "/a*b", "/a?b", "/a\"b", "/a<b", "/a>b", "/a|b", "/a\\b"}) {
    EXPECT_FALSE(writeFile(storage, name, "x")) << name;
    EXPECT_FALSE(storage.mkdirs((std::string("/d") + name).c_str())) << name;
  }
  EXPECT_EQ(storage.snapshot(), (MemorySyncStorage::Snapshot{{}, {"/d"}}));
}

TEST(MemorySyncStorage, RenameMovesAFileAndNeverOverwrites) {
  MemorySyncStorage storage;
  ASSERT_TRUE(storage.mkdirs("/Lus"));
  ASSERT_TRUE(writeFile(storage, "/a.epub", "A"));
  ASSERT_TRUE(writeFile(storage, "/Lus/B.epub", "B"));

  EXPECT_FALSE(storage.rename("/a.epub", "/Lus/B.epub"));
  EXPECT_FALSE(storage.rename("/a.epub", "/lus/b.EPUB"));
  EXPECT_FALSE(storage.rename("/a.epub", "/A.EPUB"));
  EXPECT_FALSE(storage.rename("/missing.epub", "/Lus/c.epub"));
  EXPECT_FALSE(storage.rename("/a.epub", "/Nope/a.epub"));
  EXPECT_FALSE(storage.rename("/a.epub", "/Lus/a?.epub"));
  EXPECT_EQ(storage.snapshot(), (MemorySyncStorage::Snapshot{{{"/a.epub", "A"}, {"/Lus/B.epub", "B"}}, {"/Lus"}}));

  EXPECT_TRUE(storage.rename("/A.epub", "/lus/a.epub"));
  EXPECT_EQ(storage.snapshot(), (MemorySyncStorage::Snapshot{{{"/Lus/a.epub", "A"}, {"/Lus/B.epub", "B"}}, {"/Lus"}}));
}

TEST(MemorySyncStorage, RenameMovesADirectoryWithItsContents) {
  MemorySyncStorage storage;
  ASSERT_TRUE(storage.mkdirs("/.crosspoint/epub_1/sections"));
  ASSERT_TRUE(writeFile(storage, "/.crosspoint/epub_1/sections/0.bin", "S"));
  ASSERT_TRUE(writeFile(storage, "/.crosspoint/epub_10", "other"));

  EXPECT_FALSE(storage.rename("/.crosspoint/epub_1", "/.crosspoint/epub_1/sections/x"));
  EXPECT_TRUE(storage.rename("/.crosspoint/epub_1", "/.crosspoint/epub_2"));
  EXPECT_EQ(storage.snapshot(), (MemorySyncStorage::Snapshot{
                                    {{"/.crosspoint/epub_2/sections/0.bin", "S"}, {"/.crosspoint/epub_10", "other"}},
                                    {"/.crosspoint", "/.crosspoint/epub_2", "/.crosspoint/epub_2/sections"}}));
}

TEST(MemorySyncStorage, RemoveTakesFilesAndRmdirOnlyEmptyDirectories) {
  MemorySyncStorage storage;
  ASSERT_TRUE(storage.mkdirs("/Livres/SF"));
  ASSERT_TRUE(writeFile(storage, "/Livres/SF/a.epub", "A"));

  EXPECT_FALSE(storage.remove("/Livres/SF"));
  EXPECT_FALSE(storage.rmdir("/Livres/SF/a.epub"));
  EXPECT_FALSE(storage.isEmptyDirectory("/Livres/SF"));
  EXPECT_FALSE(storage.isEmptyDirectory("/Livres/SF/a.epub"));
  EXPECT_FALSE(storage.isEmptyDirectory("/missing"));
  EXPECT_FALSE(storage.rmdir("/Livres/SF"));
  EXPECT_FALSE(storage.rmdir("/livres"));

  EXPECT_TRUE(storage.remove("/livres/sf/A.EPUB"));
  EXPECT_FALSE(storage.remove("/Livres/SF/a.epub"));
  EXPECT_TRUE(storage.isEmptyDirectory("/Livres/SF"));
  EXPECT_TRUE(storage.rmdir("/LIVRES/sf"));
  EXPECT_TRUE(storage.rmdir("/Livres"));
  EXPECT_EQ(storage.snapshot(), MemorySyncStorage::Snapshot{});
}

TEST(MemorySyncStorage, AnOpenFileCannotBeRenamedOrRemoved) {
  MemorySyncStorage storage;
  ASSERT_TRUE(storage.mkdirs("/d"));
  ASSERT_TRUE(writeFile(storage, "/d/a", "A"));
  SyncFile* file = storage.open("/d/a", SyncOpenMode::Read);
  ASSERT_NE(file, nullptr);

  EXPECT_FALSE(storage.rename("/d/a", "/b"));
  EXPECT_FALSE(storage.rename("/d", "/e"));
  EXPECT_FALSE(storage.remove("/D/A"));

  storage.close(file);
  EXPECT_TRUE(storage.rename("/d", "/e"));
  EXPECT_TRUE(storage.remove("/e/a"));
}

TEST(MemorySyncStorage, AppendReadsAnywhereAndWritesAtTheEnd) {
  MemorySyncStorage storage;
  EXPECT_EQ(storage.open("/m", SyncOpenMode::Append), nullptr);
  ASSERT_TRUE(writeFile(storage, "/m", "base"));

  SyncFile* file = storage.open("/m", SyncOpenMode::Append);
  ASSERT_NE(file, nullptr);
  ASSERT_TRUE(file->seek(1));
  uint8_t two[2];
  ASSERT_EQ(file->read(two, 2), 2);
  EXPECT_EQ(std::memcmp(two, "as", 2), 0);
  ASSERT_TRUE(file->seek(0));
  ASSERT_TRUE(file->write(reinterpret_cast<const uint8_t*>("+j"), 2));
  EXPECT_TRUE(file->flush());
  EXPECT_EQ(file->size(), 6u);
  ASSERT_TRUE(file->seek(0));
  EXPECT_EQ(readAll(*file), "base+j");
  storage.close(file);
  EXPECT_EQ(storage.snapshot().files.at("/m"), "base+j");
}

TEST(MemorySyncStorage, ReadIsReadOnlyAndTruncateWriteOnly) {
  MemorySyncStorage storage;
  ASSERT_TRUE(writeFile(storage, "/f", "old content"));

  SyncFile* reader = storage.open("/f", SyncOpenMode::Read);
  ASSERT_NE(reader, nullptr);
  EXPECT_FALSE(reader->write(reinterpret_cast<const uint8_t*>("x"), 1));
  storage.close(reader);

  SyncFile* writer = storage.open("/f", SyncOpenMode::Truncate);
  ASSERT_NE(writer, nullptr);
  EXPECT_EQ(writer->size(), 0u);
  uint8_t byte;
  EXPECT_LT(writer->read(&byte, 1), 0);
  storage.close(writer);
  EXPECT_EQ(storage.snapshot().files.at("/f"), "");
  EXPECT_EQ(storage.open("/missing", SyncOpenMode::Read), nullptr);
}

TEST(MemorySyncStorage, AtMostMaxOpenFilesAreOpenAtOnce) {
  MemorySyncStorage storage;
  ASSERT_TRUE(writeFile(storage, "/f", "x"));
  SyncFile* open[SyncStorage::MAX_OPEN_FILES];
  for (SyncFile*& file : open) {
    file = storage.open("/f", SyncOpenMode::Read);
    ASSERT_NE(file, nullptr);
  }
  EXPECT_EQ(storage.open("/f", SyncOpenMode::Read), nullptr);
  storage.close(open[0]);
  SyncFile* again = storage.open("/f", SyncOpenMode::Read);
  EXPECT_NE(again, nullptr);
  storage.close(again);
  for (size_t i = 1; i < SyncStorage::MAX_OPEN_FILES; i++) storage.close(open[i]);
  EXPECT_EQ(storage.openFiles(), 0u);
}

TEST(MemorySyncStorage, CountsMutatingCallsAndBytesWritten) {
  using Kind = MemorySyncStorage::CallKind;
  MemorySyncStorage storage;
  ASSERT_TRUE(storage.mkdirs("/d"));
  SyncFile* file = storage.open("/d/f", SyncOpenMode::Truncate);
  ASSERT_NE(file, nullptr);
  ASSERT_TRUE(file->write(reinterpret_cast<const uint8_t*>("abc"), 3));
  ASSERT_TRUE(file->write(reinterpret_cast<const uint8_t*>("de"), 2));
  ASSERT_TRUE(file->flush());
  storage.close(file);

  file = storage.open("/d/f", SyncOpenMode::Read);
  uint8_t buf[8];
  ASSERT_EQ(file->read(buf, sizeof(buf)), 5);
  storage.close(file);
  uint64_t size;
  storage.exists("/d/f");
  storage.fileSize("/d/f", size);
  storage.isEmptyDirectory("/d");

  EXPECT_FALSE(storage.rename("/d/f", "/d/f"));
  ASSERT_TRUE(storage.rename("/d/f", "/d/g"));
  ASSERT_TRUE(storage.remove("/d/g"));
  ASSERT_TRUE(storage.rmdir("/d"));

  EXPECT_EQ(storage.mutatingCalls(), 8u);
  EXPECT_EQ(storage.bytesWritten(), 5u);
  const std::vector<MemorySyncStorage::Call> expected = {
      {Kind::Mkdirs, "/d", "", true},   {Kind::Truncate, "/d/f", "", true},    {Kind::Write, "/d/f", "", true},
      {Kind::Write, "/d/f", "", true},  {Kind::Rename, "/d/f", "/d/f", false}, {Kind::Rename, "/d/f", "/d/g", true},
      {Kind::Remove, "/d/g", "", true}, {Kind::Rmdir, "/d", "", true},
  };
  EXPECT_EQ(storage.calls(), expected);
}

TEST(MemorySyncStorage, FaultTearsTheNthMutatingCallAndFailsEveryLaterOne) {
  for (const MemorySyncStorage::Tear tear : {MemorySyncStorage::Tear::None, MemorySyncStorage::Tear::Half}) {
    MemorySyncStorage storage;
    ASSERT_TRUE(storage.mkdirs("/d"));
    ASSERT_TRUE(writeFile(storage, "/d/keep", "K"));
    storage.failAt(storage.mutatingCalls() + 3, tear);

    SyncFile* file = storage.open("/d/f", SyncOpenMode::Truncate);
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(file->write(reinterpret_cast<const uint8_t*>("abcd"), 4));
    EXPECT_FALSE(file->write(reinterpret_cast<const uint8_t*>("efghij"), 6));
    EXPECT_FALSE(file->write(reinterpret_cast<const uint8_t*>("k"), 1));
    storage.close(file);

    const std::string expected = tear == MemorySyncStorage::Tear::Half ? "abcdefg" : "abcd";
    EXPECT_EQ(storage.snapshot().files.at("/d/f"), expected);
    EXPECT_EQ(storage.bytesWritten(), 1u + expected.size());

    EXPECT_FALSE(storage.mkdirs("/e"));
    EXPECT_FALSE(storage.rename("/d/f", "/d/g"));
    EXPECT_FALSE(storage.remove("/d/keep"));
    EXPECT_FALSE(storage.rmdir("/d"));
    EXPECT_EQ(storage.open("/d/h", SyncOpenMode::Truncate), nullptr);
    EXPECT_EQ(storage.snapshot(), (MemorySyncStorage::Snapshot{{{"/d/keep", "K"}, {"/d/f", expected}}, {"/d"}}));

    SyncFile* reader = storage.open("/d/keep", SyncOpenMode::Read);
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(readAll(*reader), "K");
    storage.close(reader);

    storage.clearFault();
    EXPECT_TRUE(storage.remove("/d/keep"));
  }
}

TEST(MemorySyncStorage, ResetCountersStartsCountingAndFaultsFromZero) {
  MemorySyncStorage storage;
  ASSERT_TRUE(storage.mkdirs("/d"));
  ASSERT_TRUE(writeFile(storage, "/d/f", "seed"));
  storage.resetCounters();
  EXPECT_EQ(storage.mutatingCalls(), 0u);
  EXPECT_EQ(storage.bytesWritten(), 0u);
  EXPECT_TRUE(storage.calls().empty());

  storage.failAt(2, MemorySyncStorage::Tear::None);
  EXPECT_TRUE(storage.mkdirs("/e"));
  EXPECT_FALSE(storage.mkdirs("/g"));
  EXPECT_EQ(storage.snapshot(), (MemorySyncStorage::Snapshot{{{"/d/f", "seed"}}, {"/d", "/e"}}));
}
