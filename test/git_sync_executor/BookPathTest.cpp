#include <BookCachePath.h>
#include <BookmarkPath.h>
#include <BookmarkUtil.h>
#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace {

const char* const SAMPLE_PATHS[] = {"/Livres/SF/Dune.epub", "/a.b/c", "/README.md", "/x", "/Lus/Le Petit Prince.txt",
                                    "/Livres/Été/Œuvre.xtc"};

std::string legacyCacheDir(const std::string& root, const char* prefix, const std::string& path) {
  return root + "/" + prefix + std::to_string(std::hash<std::string>{}(path));
}

}  // namespace

TEST(BookCachePath, StringOverloadMatchesLegacyReaderExpression) {
  for (const char* path : SAMPLE_PATHS) {
    EXPECT_EQ(bookCacheDir("/.crosspoint", BookCacheKind::Epub, path), legacyCacheDir("/.crosspoint", "epub_", path));
    EXPECT_EQ(bookCacheDir("/.crosspoint", BookCacheKind::Txt, path), legacyCacheDir("/.crosspoint", "txt_", path));
    EXPECT_EQ(bookCacheDir("/.crosspoint", BookCacheKind::Xtc, path), legacyCacheDir("/.crosspoint", "xtc_", path));
  }
}

TEST(BookCachePath, KeyOfViewEqualsHashOfString) {
  for (const char* path : SAMPLE_PATHS) {
    EXPECT_EQ(bookCacheKey(std::string_view(path)), std::hash<std::string>{}(std::string(path)));
  }
}

TEST(BookCachePath, BufferWriterMatchesReaderDerivation) {
  constexpr BookCacheKind KINDS[] = {BookCacheKind::Epub, BookCacheKind::Txt, BookCacheKind::Xtc};
  char out[64];
  for (const char* path : SAMPLE_PATHS) {
    for (const BookCacheKind kind : KINDS) {
      ASSERT_TRUE(formatBookCacheDir(out, sizeof(out), "/.crosspoint", kind, path));
      EXPECT_EQ(std::string(out), bookCacheDir("/.crosspoint", kind, path));
    }
  }
}

TEST(BookCachePath, BufferWriterReadsOnlyTheViewedBytes) {
  const std::string padded = "/.crosspointXX/Livres/a.epubYY";
  const std::string_view root(padded.data(), 12);
  const std::string_view path(padded.data() + 14, 14);
  char out[64];
  ASSERT_TRUE(formatBookCacheDir(out, sizeof(out), root, BookCacheKind::Epub, path));
  EXPECT_EQ(std::string(out), bookCacheDir("/.crosspoint", BookCacheKind::Epub, "/Livres/a.epub"));
}

TEST(BookCachePath, BufferTooSmallIsRefused) {
  const std::string expected = bookCacheDir("/.crosspoint", BookCacheKind::Txt, "/Livres/a.txt");
  char out[64];
  ASSERT_LT(expected.size() + 1, sizeof(out));
  EXPECT_TRUE(formatBookCacheDir(out, expected.size() + 1, "/.crosspoint", BookCacheKind::Txt, "/Livres/a.txt"));
  EXPECT_EQ(std::string(out), expected);
  EXPECT_FALSE(formatBookCacheDir(out, expected.size(), "/.crosspoint", BookCacheKind::Txt, "/Livres/a.txt"));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(formatBookCacheDir(out, 0, "/.crosspoint", BookCacheKind::Txt, "/Livres/a.txt"));
  EXPECT_FALSE(formatBookCacheDir(nullptr, sizeof(out), "/.crosspoint", BookCacheKind::Txt, "/Livres/a.txt"));
}

TEST(BookmarkPath, MatchesReaderBookmarkPath) {
  const std::string longPath = "/" + std::string(250, 'n') + ".epub";
  ASSERT_EQ(longPath.size(), 256u);
  const std::string table[] = {"/Livres/SF/Dune.epub",
                               "/a.b/c",
                               "/README.md",
                               "/x",
                               "/Lus/Le Petit Prince.epub",
                               "/Livres/Été/Œuvre complète.txt",
                               "/Livres\\SF\\Dune.epub",
                               "/.hidden",
                               "/no_extension",
                               "",
                               longPath};
  char out[300];
  for (const std::string& path : table) {
    ASSERT_TRUE(formatBookmarkPath(out, sizeof(out), path.c_str())) << path;
    EXPECT_EQ(std::string(out), BookmarkUtil::getBookmarkPath(path)) << path;
  }
}

TEST(BookmarkPath, BufferTooSmallIsRefused) {
  const std::string expected = BookmarkUtil::getBookmarkPath("/Livres/a.epub");
  char out[64];
  ASSERT_LT(expected.size() + 1, sizeof(out));
  EXPECT_TRUE(formatBookmarkPath(out, expected.size() + 1, "/Livres/a.epub"));
  EXPECT_EQ(std::string(out), expected);
  EXPECT_FALSE(formatBookmarkPath(out, expected.size(), "/Livres/a.epub"));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(formatBookmarkPath(out, 0, "/Livres/a.epub"));
  EXPECT_FALSE(formatBookmarkPath(nullptr, sizeof(out), "/Livres/a.epub"));
}
