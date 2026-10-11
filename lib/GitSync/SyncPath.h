#pragma once

#include <cstddef>

// Longest repository-relative path, in UTF-8 bytes, that the sync may write.
constexpr size_t MAX_SYNC_PATH_BYTES = 255;

enum class SyncPathVerdict { Syncable, Hidden, Malformed, TooLong };

// Reads exactly len bytes. TooLong wins over Malformed, which wins over Hidden.
// Hidden: a segment starts with '.'. Malformed: empty path or segment, leading '/', a ".." segment,
// a byte below 0x20, or a segment starting with a space.
SyncPathVerdict classifySyncPath(const char* path, size_t len);

bool isSyncablePath(const char* path, size_t len);
