#include <GitSyncErrorCode.h>
#include <gtest/gtest.h>

TEST(GitSyncErrorCode, CleanRunIsTheRunErrorAndTheHeapOnly) {
  GitSyncCodeInputs in;
  in.lowestFreeHeap = 38 * 1024;
  char code[GIT_SYNC_CODE_CAPACITY];
  formatGitSyncCode(code, sizeof(code), in);
  EXPECT_STREQ(code, "E0.0 H38");
}

TEST(GitSyncErrorCode, FailedFileShowsEveryFailedRequestAndTheBytesHeld) {
  GitSyncCodeInputs in;
  in.runError = 0;
  in.fileError = 2;
  in.fileHeld = 3u * 1024 * 1024;
  in.fileSize = 15u * 1024 * 1024;
  in.apiError = 1;
  in.apiStatus = -1;
  in.rangedError = 5;
  in.rangedStatus = 404;
  in.lowestFreeHeap = 38 * 1024 + 900;
  char code[GIT_SYNC_CODE_CAPACITY];
  formatGitSyncCode(code, sizeof(code), in);
  EXPECT_STREQ(code, "E0.2 A1/-1 R5/404 3072/15360K H38");
}

TEST(GitSyncErrorCode, LargestValuesFitTheCapacity) {
  GitSyncCodeInputs in;
  in.runError = 255;
  in.fileError = 255;
  in.fileHeld = UINT64_MAX;
  in.fileSize = UINT64_MAX;
  in.apiError = 255;
  in.apiStatus = INT16_MIN;
  in.rangedError = 255;
  in.rangedStatus = INT16_MIN;
  in.lowestFreeHeap = UINT32_MAX;
  char code[GIT_SYNC_CODE_CAPACITY];
  formatGitSyncCode(code, sizeof(code), in);
  EXPECT_EQ(std::string(code),
            "E255.255 A255/-32768 R255/-32768 18014398509481983/18014398509481983K H4194303");
}
