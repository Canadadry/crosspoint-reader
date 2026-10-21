#include <gtest/gtest.h>

#include <string>

#include "ProtectedPath.h"

TEST(ProtectedPath, RejectsFilesInsideCrosspointFolder) {
  EXPECT_TRUE(isProtectedStoragePath("/.crosspoint/github-sync.json"));
  EXPECT_TRUE(isProtectedStoragePath("/.crosspoint/wifi.json"));
}

TEST(ProtectedPath, RejectsFilesInsideSystemFolders) {
  EXPECT_TRUE(isProtectedStoragePath("/XTCache/x.bin"));
  EXPECT_TRUE(isProtectedStoragePath("/System Volume Information/x"));
}

TEST(ProtectedPath, RejectsHiddenSegmentAnywhere) {
  EXPECT_TRUE(isProtectedStoragePath("/a/.git/b"));
  EXPECT_TRUE(isProtectedStoragePath("/a/b/.hidden"));
}

TEST(ProtectedPath, RejectsDotSegmentsOfUnnormalisedPaths) {
  EXPECT_TRUE(isProtectedStoragePath("/Livres/../.crosspoint"));
  EXPECT_TRUE(isProtectedStoragePath("//.crosspoint//wifi.json"));
}

TEST(ProtectedPath, AcceptsOrdinaryPaths) {
  EXPECT_FALSE(isProtectedStoragePath("/Livres/\xC3\x89t\xC3\xA9.epub"));
  EXPECT_FALSE(isProtectedStoragePath("/a/b.epub"));
  EXPECT_FALSE(isProtectedStoragePath("/"));
  EXPECT_FALSE(isProtectedStoragePath("/XTCache.epub"));
}
