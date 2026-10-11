#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "GitHubApi.h"

namespace {

GitSyncConfig makeConfig(const char* owner, const char* repo, const char* branch) {
  GitSyncConfig config = {};
  strncpy(config.owner, owner, sizeof(config.owner) - 1);
  strncpy(config.repo, repo, sizeof(config.repo) - 1);
  strncpy(config.branch, branch, sizeof(config.branch) - 1);
  strncpy(config.token, "ghp_x", sizeof(config.token) - 1);
  return config;
}

}  // namespace

TEST(GitHubApi, TreesUrlListsBranchRecursively) {
  const GitSyncConfig config = makeConfig("Canadadry", "books", "main");
  char url[GITHUB_API_URL_CAPACITY];
  ASSERT_TRUE(formatTreesUrl(url, sizeof(url), config));
  EXPECT_STREQ(url, "https://api.github.com/repos/Canadadry/books/git/trees/main?recursive=1");
}

TEST(GitHubApi, BlobUrlNamesShaInLowerCaseHex) {
  const GitSyncConfig config = makeConfig("Canadadry", "books", "main");
  uint8_t sha[GitBlobHasher::SHA_SIZE];
  ASSERT_TRUE(shaFromHex("CC858913C6F1452C9CBD67097E1D97D4A4450A08", SHA_HEX_LENGTH, sha));
  char url[GITHUB_API_URL_CAPACITY];
  ASSERT_TRUE(formatBlobUrl(url, sizeof(url), config, sha));
  EXPECT_STREQ(url, "https://api.github.com/repos/Canadadry/books/git/blobs/cc858913c6f1452c9cbd67097e1d97d4a4450a08");
}

TEST(GitHubApi, BranchWithSlashIsKeptVerbatim) {
  const GitSyncConfig config = makeConfig("jerome", "books", "feature/new-shelf");
  char url[GITHUB_API_URL_CAPACITY];
  ASSERT_TRUE(formatTreesUrl(url, sizeof(url), config));
  EXPECT_STREQ(url, "https://api.github.com/repos/jerome/books/git/trees/feature/new-shelf?recursive=1");
}

TEST(GitHubApi, LongestFieldsFillCapacityExactly) {
  const std::string owner(GitSyncConfig::OWNER_MAX, 'o');
  const std::string repo(GitSyncConfig::REPO_MAX, 'r');
  const std::string branch(GitSyncConfig::BRANCH_MAX, 'b');
  const GitSyncConfig config = makeConfig(owner.c_str(), repo.c_str(), branch.c_str());
  char url[GITHUB_API_URL_CAPACITY];
  ASSERT_TRUE(formatTreesUrl(url, sizeof(url), config));
  EXPECT_EQ(strlen(url) + 1, GITHUB_API_URL_CAPACITY);
  EXPECT_EQ(std::string(url),
            "https://api.github.com/repos/" + owner + "/" + repo + "/git/trees/" + branch + "?recursive=1");
}

TEST(GitHubApi, LongestBlobUrlFitsCapacity) {
  const std::string owner(GitSyncConfig::OWNER_MAX, 'o');
  const std::string repo(GitSyncConfig::REPO_MAX, 'r');
  const GitSyncConfig config = makeConfig(owner.c_str(), repo.c_str(), "main");
  const uint8_t sha[GitBlobHasher::SHA_SIZE] = {};
  char url[GITHUB_API_URL_CAPACITY];
  ASSERT_TRUE(formatBlobUrl(url, sizeof(url), config, sha));
  EXPECT_EQ(std::string(url),
            "https://api.github.com/repos/" + owner + "/" + repo + "/git/blobs/" + std::string(40, '0'));
}

TEST(GitHubApi, CapacityIs448Bytes) { EXPECT_EQ(GITHUB_API_URL_CAPACITY, 448u); }

TEST(GitHubApi, TreesUrlOneByteShortIsRefusedEmpty) {
  const std::string owner(GitSyncConfig::OWNER_MAX, 'o');
  const std::string repo(GitSyncConfig::REPO_MAX, 'r');
  const std::string branch(GitSyncConfig::BRANCH_MAX, 'b');
  const GitSyncConfig config = makeConfig(owner.c_str(), repo.c_str(), branch.c_str());
  char url[GITHUB_API_URL_CAPACITY];
  memset(url, 'x', sizeof(url));
  EXPECT_FALSE(formatTreesUrl(url, sizeof(url) - 1, config));
  EXPECT_STREQ(url, "");
}

TEST(GitHubApi, BlobUrlOneByteShortIsRefusedEmpty) {
  const GitSyncConfig config = makeConfig("Canadadry", "books", "main");
  const uint8_t sha[GitBlobHasher::SHA_SIZE] = {};
  const size_t needed = sizeof("https://api.github.com/repos/Canadadry/books/git/blobs/") - 1 + SHA_HEX_LENGTH + 1;
  char url[GITHUB_API_URL_CAPACITY];
  ASSERT_TRUE(formatBlobUrl(url, needed, config, sha));
  memset(url, 'x', sizeof(url));
  EXPECT_FALSE(formatBlobUrl(url, needed - 1, config, sha));
  EXPECT_STREQ(url, "");
}

TEST(GitHubApi, EmptyFieldIsRefusedEmpty) {
  const GitSyncConfig configs[] = {makeConfig("", "books", "main"), makeConfig("jerome", "", "main"),
                                   makeConfig("jerome", "books", "")};
  for (const GitSyncConfig& config : configs) {
    char url[GITHUB_API_URL_CAPACITY];
    memset(url, 'x', sizeof(url));
    EXPECT_FALSE(formatTreesUrl(url, sizeof(url), config));
    EXPECT_STREQ(url, "");
  }
}

TEST(GitHubApi, BlobUrlWithEmptyOwnerOrRepoIsRefusedEmpty) {
  const GitSyncConfig configs[] = {makeConfig("", "books", "main"), makeConfig("jerome", "", "main")};
  const uint8_t sha[GitBlobHasher::SHA_SIZE] = {};
  for (const GitSyncConfig& config : configs) {
    char url[GITHUB_API_URL_CAPACITY];
    memset(url, 'x', sizeof(url));
    EXPECT_FALSE(formatBlobUrl(url, sizeof(url), config, sha));
    EXPECT_STREQ(url, "");
  }
}

TEST(GitHubApi, ZeroCapacityIsRefusedWithoutWriting) {
  const GitSyncConfig config = makeConfig("jerome", "books", "main");
  const uint8_t sha[GitBlobHasher::SHA_SIZE] = {};
  char url[1] = {'x'};
  EXPECT_FALSE(formatTreesUrl(url, 0, config));
  EXPECT_FALSE(formatBlobUrl(url, 0, config, sha));
  EXPECT_EQ(url[0], 'x');
}

TEST(GitHubApi, UnterminatedFieldIsRefusedEmpty) {
  GitSyncConfig config = makeConfig("jerome", "books", "main");
  memset(config.repo, 'r', sizeof(config.repo));
  const uint8_t sha[GitBlobHasher::SHA_SIZE] = {};
  char url[GITHUB_API_URL_CAPACITY];
  EXPECT_FALSE(formatTreesUrl(url, sizeof(url), config));
  EXPECT_STREQ(url, "");
  EXPECT_FALSE(formatBlobUrl(url, sizeof(url), config, sha));
  EXPECT_STREQ(url, "");
}

TEST(GitHubApi, RequestConstantsMatchVerifiedCalls) {
  EXPECT_STREQ(GITHUB_ACCEPT_JSON, "application/vnd.github+json");
  EXPECT_STREQ(GITHUB_ACCEPT_RAW, "application/vnd.github.raw+json");
  EXPECT_STREQ(GITHUB_BASIC_AUTH_USER, "x");
}
