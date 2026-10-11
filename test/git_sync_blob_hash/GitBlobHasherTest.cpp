#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "GitBlobHasher.h"

namespace {

std::string toHex(const uint8_t sha[GitBlobHasher::SHA_SIZE]) {
  std::string hex;
  char byte[3];
  for (size_t i = 0; i < GitBlobHasher::SHA_SIZE; i++) {
    snprintf(byte, sizeof(byte), "%02x", sha[i]);
    hex += byte;
  }
  return hex;
}

std::string hashAll(const std::vector<uint8_t>& content) {
  GitBlobHasher hasher;
  hasher.begin(content.size());
  hasher.update(content.data(), content.size());
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  if (!hasher.finish(sha)) return "finish failed";
  return toHex(sha);
}

std::vector<uint8_t> modPattern(size_t size) {
  std::vector<uint8_t> content(size);
  for (size_t i = 0; i < size; i++) content[i] = static_cast<uint8_t>(i % 251);
  return content;
}

}  // namespace

TEST(GitBlobHasher, EmptyFileGivesEmptyBlobSha) {
  GitBlobHasher hasher;
  hasher.begin(0);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
}

TEST(GitBlobHasher, ContentFedInOneCallMatchesGit) {
  const uint8_t content[] = {'h', 'e', 'l', 'l', 'o', '\n'};
  GitBlobHasher hasher;
  hasher.begin(sizeof(content));
  hasher.update(content, sizeof(content));
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), "ce013625030ba8dba906f756967f9e9ca394464a");
}

TEST(GitBlobHasher, ContentFedByteByByteMatchesGit) {
  const uint8_t content[] = {'h', 'e', 'l', 'l', 'o', '\n'};
  GitBlobHasher hasher;
  hasher.begin(sizeof(content));
  for (uint8_t byte : content) hasher.update(&byte, 1);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), "ce013625030ba8dba906f756967f9e9ca394464a");
}

TEST(GitBlobHasher, EmptyUpdateLeavesEmptyBlobSha) {
  GitBlobHasher hasher;
  hasher.begin(0);
  hasher.update(nullptr, 0);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
}

TEST(GitBlobHasher, OneMebibyteInUnevenChunksMatchesGit) {
  const std::vector<uint8_t> content = modPattern(1048576);
  constexpr size_t CHUNK_SIZES[] = {1, 63, 64, 65, 7, 4096, 1000, 4097};
  GitBlobHasher hasher;
  hasher.begin(content.size());
  size_t offset = 0;
  for (size_t i = 0; offset < content.size(); i++) {
    const size_t chunk = std::min(CHUNK_SIZES[i % std::size(CHUNK_SIZES)], content.size() - offset);
    hasher.update(content.data() + offset, chunk);
    offset += chunk;
  }
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), "b859c508ba043c1601650b2010f9b6e6eccc0a7f");
}

TEST(GitBlobHasher, OneMebibyteInOneCallMatchesGit) {
  EXPECT_EQ(hashAll(modPattern(1048576)), "b859c508ba043c1601650b2010f9b6e6eccc0a7f");
}

TEST(GitBlobHasher, ContentSizesMultipleOfBlockMatchGit) {
  EXPECT_EQ(hashAll(modPattern(64)), "96eb299ab61d459148b19b03f71386abcec74669");
  EXPECT_EQ(hashAll(modPattern(128)), "7e1cc9de8f128c86383ef196743f3133260c16fc");
}

TEST(GitBlobHasher, PaddingBoundariesMatchGit) {
  EXPECT_EQ(hashAll(std::vector<uint8_t>(47, 'a')), "5e3bf7e629b4908cce461530c17a64365cb47303");
  EXPECT_EQ(hashAll(std::vector<uint8_t>(48, 'a')), "12d42395b020f44bb7710113b31f688d0ebeda7c");
  EXPECT_EQ(hashAll(std::vector<uint8_t>(56, 'a')), "1f973e890f52da1f22fa7e5620a628bc4ee74cb3");
}

TEST(GitBlobHasher, AllByteValuesMatchGit) {
  std::vector<uint8_t> content(256);
  for (size_t i = 0; i < content.size(); i++) content[i] = static_cast<uint8_t>(i);
  EXPECT_EQ(hashAll(content), "c86626638e0bc8cf47ca49bb1525b40e9737ee64");
}

TEST(GitBlobHasher, Utf8ContentMatchesGit) {
  const std::string text = "Livres/\xC3\x89t\xC3\xA9.epub";
  EXPECT_EQ(hashAll(std::vector<uint8_t>(text.begin(), text.end())), "50bf281754b0e8ac33dbe024ff687c4435f258c7");
}

TEST(GitBlobHasher, BeginAgainHashesNextFileIndependently) {
  GitBlobHasher hasher;
  uint8_t sha[GitBlobHasher::SHA_SIZE];

  const uint8_t hello[] = {'h', 'e', 'l', 'l', 'o', '\n'};
  hasher.begin(sizeof(hello));
  hasher.update(hello, sizeof(hello));
  ASSERT_TRUE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), "ce013625030ba8dba906f756967f9e9ca394464a");

  hasher.begin(0);
  ASSERT_TRUE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
}

TEST(GitBlobHasher, ShortContentFailsWithZeroSha) {
  const uint8_t content[] = {'h', 'e', 'l', 'l', 'o'};
  GitBlobHasher hasher;
  hasher.begin(6);
  hasher.update(content, sizeof(content));
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  memset(sha, 0xAA, sizeof(sha));
  EXPECT_FALSE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), std::string(40, '0'));
}

TEST(GitBlobHasher, LongContentFailsWithZeroSha) {
  const uint8_t content[] = {'h', 'e', 'l', 'l', 'o', '\n', '!'};
  GitBlobHasher hasher;
  hasher.begin(6);
  hasher.update(content, sizeof(content));
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  memset(sha, 0xAA, sizeof(sha));
  EXPECT_FALSE(hasher.finish(sha));
  EXPECT_EQ(toHex(sha), std::string(40, '0'));
}

TEST(ShaHex, FormatsLowerCaseAndTerminates) {
  GitBlobHasher hasher;
  hasher.begin(0);
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(hasher.finish(sha));
  char hex[41];
  memset(hex, 'X', sizeof(hex));
  shaToHex(sha, hex);
  EXPECT_EQ(hex[40], '\0');
  EXPECT_STREQ(hex, "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
}

TEST(ShaHex, ParsedVectorsFormatBackIdentically) {
  const char* vectors[] = {
      "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391", "ce013625030ba8dba906f756967f9e9ca394464a",
      "b859c508ba043c1601650b2010f9b6e6eccc0a7f", "96eb299ab61d459148b19b03f71386abcec74669",
      "7e1cc9de8f128c86383ef196743f3133260c16fc", "5e3bf7e629b4908cce461530c17a64365cb47303",
      "12d42395b020f44bb7710113b31f688d0ebeda7c", "1f973e890f52da1f22fa7e5620a628bc4ee74cb3",
      "c86626638e0bc8cf47ca49bb1525b40e9737ee64", "50bf281754b0e8ac33dbe024ff687c4435f258c7",
  };
  for (const char* vector : vectors) {
    uint8_t sha[GitBlobHasher::SHA_SIZE];
    ASSERT_TRUE(shaFromHex(vector, strlen(vector), sha)) << vector;
    char hex[SHA_HEX_LENGTH + 1];
    shaToHex(sha, hex);
    EXPECT_STREQ(hex, vector);
  }
}

TEST(ShaHex, ParsedShaEqualsComputedSha) {
  GitBlobHasher hasher;
  hasher.begin(0);
  uint8_t computed[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(hasher.finish(computed));
  uint8_t parsed[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(shaFromHex("e69de29bb2d1d6434b8b29ae775ad8c2e48c5391", SHA_HEX_LENGTH, parsed));
  EXPECT_EQ(memcmp(parsed, computed, sizeof(parsed)), 0);
}

TEST(ShaHex, UpperCaseParsesToSameBytes) {
  uint8_t lower[GitBlobHasher::SHA_SIZE];
  uint8_t upper[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(shaFromHex("e69de29bb2d1d6434b8b29ae775ad8c2e48c5391", SHA_HEX_LENGTH, lower));
  ASSERT_TRUE(shaFromHex("E69DE29BB2D1D6434B8B29AE775AD8C2E48C5391", SHA_HEX_LENGTH, upper));
  EXPECT_EQ(memcmp(lower, upper, sizeof(lower)), 0);
}

TEST(ShaHex, RejectsWrongLength) {
  const std::string valid = "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391";
  const std::string longer = valid + "0";
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  EXPECT_FALSE(shaFromHex(valid.c_str(), 39, sha));
  EXPECT_FALSE(shaFromHex(longer.c_str(), longer.size(), sha));
  EXPECT_FALSE(shaFromHex("", 0, sha));
}

TEST(ShaHex, RejectsNonHexCharacterAnywhere) {
  const std::string valid = "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391";
  for (char bad : {'g', ' ', '-'}) {
    for (size_t position : {size_t{0}, size_t{20}, size_t{39}}) {
      std::string hex = valid;
      hex[position] = bad;
      uint8_t sha[GitBlobHasher::SHA_SIZE];
      EXPECT_FALSE(shaFromHex(hex.c_str(), hex.size(), sha)) << "'" << bad << "' at " << position;
    }
  }
}

TEST(ShaHex, ReadsOnlyGivenLengthOfUnterminatedBuffer) {
  const char buffer[] = {'e', '6', '9', 'd', 'e', '2', '9', 'b', 'b', '2', 'd', '1', 'd', '6', '4',
                         '3', '4', 'b', '8', 'b', '2', '9', 'a', 'e', '7', '7', '5', 'a', 'd', '8',
                         'c', '2', 'e', '4', '8', 'c', '5', '3', '9', '1', 'z', 'z', '"'};
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(shaFromHex(buffer, SHA_HEX_LENGTH, sha));
  char hex[SHA_HEX_LENGTH + 1];
  shaToHex(sha, hex);
  EXPECT_STREQ(hex, "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
}
