#pragma once

#include <cstddef>
#include <cstdint>

#include "GitBlobHasher.h"
#include "RecordStream.h"

// Called after each chunk read; a null onChunk is never called.
struct GitBlobHashTick {
  void* ctx = nullptr;
  void (*onChunk)(void* ctx) = nullptr;
};

// Hashes source from its current position to its end as a git blob of expectedSize bytes, reading
// bufferLen bytes at a time into the caller's buffer. False, with outSha zero-filled, on a missing
// buffer, a read error, or when the bytes read differ from expectedSize; a longer source stops at the
// first chunk past it.
bool hashGitBlobSource(RecordSource& source, uint64_t expectedSize, uint8_t* buffer, size_t bufferLen,
                       GitBlobHasher& hasher, uint8_t outSha[GitBlobHasher::SHA_SIZE], const GitBlobHashTick& tick);
