#pragma once

#include <cstddef>
#include <cstdint>

// What a sync's end screen code is built from; every field 0 when unknown.
struct GitSyncCodeInputs {
  uint8_t runError = 0;     // GitSyncError
  uint8_t fileError = 0;    // SyncOpOutcome of the last abandoned file
  uint64_t fileHeld = 0;    // bytes of that file's blob kept for the next run
  uint64_t fileSize = 0;    // that blob's size
  uint8_t apiError = 0;     // HttpDownloader::DownloadError of the last failed API blob request
  int16_t apiStatus = 0;    // its HTTP status, -1 when none was read
  uint8_t rangedError = 0;  // the same for the last failed ranged request
  int16_t rangedStatus = 0;
  uint32_t lowestFreeHeap = 0;  // bytes
};

// The longest code, "E255.255 A255/-32768 R255/-32768 <u64/1024>/<u64/1024>K H<u32/1024>", and its terminator.
constexpr size_t GIT_SYNC_CODE_CAPACITY = 96;

// "E<run>.<file>[ A<err>/<status>][ R<err>/<status>][ <heldK>/<sizeK>K] H<heapK>": a request part only when that
// request failed, the bytes only for a file error with a known size.
void formatGitSyncCode(char* out, size_t capacity, const GitSyncCodeInputs& in);
