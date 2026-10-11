#include "SyncPath.h"

#include <cstdint>

SyncPathVerdict classifySyncPath(const char* path, size_t len) {
  if (len > MAX_SYNC_PATH_BYTES) return SyncPathVerdict::TooLong;

  bool malformed = false;
  bool hidden = false;
  size_t segmentStart = 0;
  // i == len closes the last segment, as a '/' would.
  for (size_t i = 0; i <= len; i++) {
    if (i == len || path[i] == '/') {
      const size_t segmentLen = i - segmentStart;
      if (segmentLen == 0) malformed = true;
      if (segmentLen == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.') malformed = true;
      segmentStart = i + 1;
      continue;
    }
    const auto byte = static_cast<uint8_t>(path[i]);
    if (byte < 0x20) malformed = true;
    if (i == segmentStart) {
      // SdFat skips leading spaces, so " .x" would open ".x".
      if (byte == ' ') malformed = true;
      if (byte == '.') hidden = true;
    }
  }

  if (malformed) return SyncPathVerdict::Malformed;
  return hidden ? SyncPathVerdict::Hidden : SyncPathVerdict::Syncable;
}

bool isSyncablePath(const char* path, size_t len) { return classifySyncPath(path, len) == SyncPathVerdict::Syncable; }
