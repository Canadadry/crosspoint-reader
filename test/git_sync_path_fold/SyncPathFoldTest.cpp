#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "SyncPathFold.h"

namespace {

bool equalFolded(const std::string& a, const std::string& b) {
  return syncPathsEqualFolded(a.data(), a.size(), b.data(), b.size());
}

bool fatSafe(const std::string& path) { return isFatSafeName(path.data(), path.size()); }

uint32_t key(const std::string& path) { return syncPathKey(path.data(), path.size()); }

}  // namespace

TEST(SyncPathFoldTest, AsciiCaseIsFolded) {
  EXPECT_TRUE(equalFolded("Books/Book.epub", "books/BOOK.EPUB"));
  EXPECT_FALSE(equalFolded("Books/Book.epub", "Books/Boot.epub"));
  EXPECT_FALSE(equalFolded("Book.epub", "Book.epub2"));
}

TEST(SyncPathFoldTest, Latin1CaseIsFolded) {
  EXPECT_TRUE(equalFolded("Été", "ÉTÉ"));
  EXPECT_TRUE(equalFolded("é", "É"));
  EXPECT_TRUE(equalFolded("Auteurs/Hugo/Les Misérables.epub", "AUTEURS/hugo/les misÉrables.EPUB"));
  EXPECT_TRUE(equalFolded("ÿ", "Ÿ"));
  EXPECT_FALSE(equalFolded("é", "è"));
  EXPECT_FALSE(equalFolded("é", "e"));
}

TEST(SyncPathFoldTest, GreekAndCyrillicCaseIsFolded) {
  EXPECT_TRUE(equalFolded("Ομηρος/Οδύσσεια.epub", "ΟΜΗΡΟΣ/ΟΔΎΣΣΕΙΑ.EPUB"));
  EXPECT_TRUE(equalFolded("έ", "Έ"));
  EXPECT_TRUE(equalFolded("ς", "Σ"));
  EXPECT_TRUE(equalFolded("Толстой/Война и мир.epub", "ТОЛСТОЙ/ВОЙНА И МИР.EPUB"));
  EXPECT_TRUE(equalFolded("ё", "Ё"));
  EXPECT_TRUE(equalFolded("ѣ", "Ѣ"));
  EXPECT_FALSE(equalFolded("а", "a"));
}

TEST(SyncPathFoldTest, SharpSIsNotFolded) {
  EXPECT_FALSE(equalFolded("ß", "SS"));
  EXPECT_FALSE(equalFolded("ß", "ẞ"));
  EXPECT_TRUE(equalFolded("Straße", "STRAßE"));
}

TEST(SyncPathFoldTest, GeorgianUsesTheNegativeOffsetRange) { EXPECT_TRUE(equalFolded("ⴀ", "Ⴀ")); }

TEST(SyncPathFoldTest, InvalidUtf8BytesAreComparedUnfolded) {
  EXPECT_TRUE(equalFolded("caf\xE9.epub", "CAF\xE9.EPUB"));
  EXPECT_FALSE(equalFolded("caf\xE9", "caf\xC9"));
  EXPECT_FALSE(equalFolded("\xC9", "É"));
  EXPECT_FALSE(equalFolded("\xC1\x81", "a"));
  EXPECT_FALSE(equalFolded("\xED\xA0\x80", "\xED\xA0\x81"));
  EXPECT_TRUE(
      equalFolded("\xC3"
                  "a",
                  "\xC3"
                  "A"));
  EXPECT_FALSE(equalFolded("\xC3", "\xC3\xA9"));
}

TEST(SyncPathFoldTest, FourByteSequencesAreComparedUnfolded) {
  EXPECT_TRUE(equalFolded("𐐨/Book.epub", "𐐨/BOOK.EPUB"));
  EXPECT_FALSE(equalFolded("𐐨", "𐐀"));
  EXPECT_FALSE(equalFolded("😀", "😁"));
}

TEST(SyncPathFoldTest, KeysAreEqualWheneverPathsAreEqualFolded) {
  const std::pair<std::string, std::string> pairs[] = {
      {"Books/Book.epub", "books/BOOK.EPUB"},
      {"Été", "ÉTÉ"},
      {"ς", "Σ"},
      {"Толстой/Война и мир.epub", "ТОЛСТОЙ/ВОЙНА И МИР.EPUB"},
      {"ⴀ", "Ⴀ"},
      {"caf\xE9", "CAF\xE9"},
      {"𐐨/Book.epub", "𐐨/BOOK.EPUB"},
      {"", ""},
  };
  for (const auto& [a, b] : pairs) {
    SCOPED_TRACE(a + " / " + b);
    ASSERT_TRUE(equalFolded(a, b));
    EXPECT_EQ(key(a), key(b));
  }
}

TEST(SyncPathFoldTest, KeysDifferForDifferentPaths) {
  EXPECT_NE(key("a.epub"), key("b.epub"));
  EXPECT_NE(key("\xC9"), key("É"));
  EXPECT_NE(key("𐐨"), key("𐐀"));
  EXPECT_NE(key("ab"), key("ba"));
}

TEST(SyncPathFoldTest, EveryFatForbiddenCharacterIsRejected) {
  for (const char forbidden : {':', '*', '?', '"', '<', '>', '|', '\\'}) {
    const std::string name = std::string("Auteurs/Book") + forbidden + "1.epub";
    SCOPED_TRACE(name);
    EXPECT_FALSE(fatSafe(name));
  }
}

TEST(SyncPathFoldTest, NestedUtf8NamesAreFatSafe) {
  for (const char* path : {"book.epub", "Auteurs/Hugo/Les Misérables.epub", "Толстой/Война и мир.epub",
                           "Ομηρος/Οδύσσεια.epub", "日本/本 (2024) [v1], #1 & co; +x=y'z.epub", "😀/𐐨.epub"}) {
    SCOPED_TRACE(path);
    EXPECT_TRUE(fatSafe(path));
  }
}
