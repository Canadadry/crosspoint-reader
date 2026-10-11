#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include "StreamingJsonParser.h"
#include "SyncPath.h"

namespace {

void expectVerdict(const std::string& path, SyncPathVerdict expected) {
  SCOPED_TRACE("path: \"" + path + "\"");
  EXPECT_EQ(classifySyncPath(path.data(), path.size()), expected);
  EXPECT_EQ(isSyncablePath(path.data(), path.size()), expected == SyncPathVerdict::Syncable);
}

}  // namespace

TEST(SyncPathTest, PlainFileIsSyncable) { expectVerdict("book.epub", SyncPathVerdict::Syncable); }

TEST(SyncPathTest, DotSegmentAnywhereIsHidden) {
  for (const char* path : {".crosspoint/x", "a/.git/b", ".gitignore", ".github/workflows/ci.yml", ".atelier/notes.md",
                           "docs/.hidden/x.epub", "a/b/.c", ".", "a/./b"}) {
    expectVerdict(path, SyncPathVerdict::Hidden);
  }
}

TEST(SyncPathTest, DotInsideNameIsSyncable) {
  for (const char* path : {"Vol. 2/book.v1.epub", "a..b.epub", "a.", "Auteurs/Hugo/Les Misérables.epub"}) {
    expectVerdict(path, SyncPathVerdict::Syncable);
  }
}

TEST(SyncPathTest, EmptyPathIsMalformed) {
  expectVerdict("", SyncPathVerdict::Malformed);
  EXPECT_EQ(classifySyncPath(nullptr, 0), SyncPathVerdict::Malformed);
  EXPECT_FALSE(isSyncablePath(nullptr, 0));
}

TEST(SyncPathTest, EmptySegmentIsMalformed) {
  for (const char* path : {"a//b", "/x.epub", "/", "a/", "/a/"}) {
    expectVerdict(path, SyncPathVerdict::Malformed);
  }
}

TEST(SyncPathTest, ParentSegmentIsMalformed) {
  for (const char* path : {"a/../b", "..", "../x", "a/..", ".git/../x", "a/.b//c"}) {
    expectVerdict(path, SyncPathVerdict::Malformed);
  }
}

TEST(SyncPathTest, ControlByteIsMalformed) {
  for (const std::string& path :
       {std::string("a\tb.epub"), std::string("a\nb"), std::string("a\0b.epub", 8), std::string("book.epub\x1f", 10)}) {
    expectVerdict(path, SyncPathVerdict::Malformed);
  }
}

TEST(SyncPathTest, SegmentStartingWithSpaceIsMalformed) {
  for (const char* path : {" .crosspoint/github-sync.json", "a/ ../b", " book.epub"}) {
    expectVerdict(path, SyncPathVerdict::Malformed);
  }
}

TEST(SyncPathTest, SpaceInsideNameIsSyncable) {
  for (const char* path : {"book .epub", "a b/c d.epub"}) {
    expectVerdict(path, SyncPathVerdict::Syncable);
  }
}

TEST(SyncPathTest, MaxLengthIsCountedInBytes) {
  EXPECT_EQ(MAX_SYNC_PATH_BYTES, 255u);
  EXPECT_LT(MAX_SYNC_PATH_BYTES, StreamingJsonParser::TOKEN_BUF_SIZE);

  expectVerdict(std::string(255, 'a'), SyncPathVerdict::Syncable);
  expectVerdict(std::string(256, 'a'), SyncPathVerdict::TooLong);

  std::string twoByteChars;
  for (int i = 0; i < 127; i++) twoByteChars += "é";
  expectVerdict(twoByteChars + "a", SyncPathVerdict::Syncable);
  expectVerdict(twoByteChars + "é", SyncPathVerdict::TooLong);
}

TEST(SyncPathTest, TooLongTakesPrecedence) {
  expectVerdict("." + std::string(255, 'a'), SyncPathVerdict::TooLong);

  std::string climbing;
  while (climbing.size() < 600) climbing += "../";
  climbing.resize(600);
  expectVerdict(climbing, SyncPathVerdict::TooLong);
}

TEST(SyncPathTest, Utf8NamesAreSyncable) {
  for (const char* path :
       {"Livres/Été.epub", "日本語/本.epub", "Книги/Война и мир.epub", "Emoji/\xF0\x9F\x93\x9A.epub"}) {
    expectVerdict(path, SyncPathVerdict::Syncable);
  }
}

TEST(SyncPathTest, ReadsOnlyLenBytes) {
  const std::string source = "a.epub";
  auto exact = std::make_unique<char[]>(source.size());
  memcpy(exact.get(), source.data(), source.size());
  EXPECT_EQ(classifySyncPath(exact.get(), source.size()), SyncPathVerdict::Syncable);

  // The bytes after len would make the path malformed.
  const char trailing[] = "a.epub/../x";
  EXPECT_EQ(classifySyncPath(trailing, 6), SyncPathVerdict::Syncable);
  EXPECT_TRUE(isSyncablePath(trailing, 6));
}
