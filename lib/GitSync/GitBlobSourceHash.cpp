#include "GitBlobSourceHash.h"

#include <cstring>

bool hashGitBlobSource(RecordSource& source, uint64_t expectedSize, uint8_t* buffer, size_t bufferLen,
                       GitBlobHasher& hasher, uint8_t outSha[GitBlobHasher::SHA_SIZE], const GitBlobHashTick& tick) {
  memset(outSha, 0, GitBlobHasher::SHA_SIZE);
  if (!buffer || bufferLen == 0) return false;
  hasher.begin(expectedSize);
  uint64_t total = 0;
  for (;;) {
    const int got = source.read(buffer, bufferLen);
    if (got < 0) return false;
    if (got == 0) break;
    total += static_cast<uint64_t>(got);
    // Stops a longer file at the first chunk past the size instead of reading it all.
    if (total > expectedSize) return false;
    hasher.update(buffer, static_cast<size_t>(got));
    if (tick.onChunk) tick.onChunk(tick.ctx);
  }
  return hasher.finish(outSha);
}
