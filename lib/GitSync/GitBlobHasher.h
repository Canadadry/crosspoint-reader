#pragma once

#include <cstddef>
#include <cstdint>

// Git blob SHA-1: SHA1("blob <size>\0" + content), computed incrementally.
class GitBlobHasher {
 public:
  static constexpr size_t SHA_SIZE = 20;
  static constexpr size_t STATE_WORDS = SHA_SIZE / 4;

  // Resets the hasher and absorbs the "blob <size>\0" header.
  void begin(uint64_t size);
  void update(const uint8_t* data, size_t len);
  // Returns false and zero-fills out when the content fed differs from the size given to begin().
  // begin() must be called again before the next use.
  bool finish(uint8_t out[SHA_SIZE]);

 private:
  static constexpr size_t BLOCK_SIZE = 64;

  void absorb(const uint8_t* data, size_t len);
  void compressBlock();

  uint32_t state[STATE_WORDS] = {};
  uint8_t block[BLOCK_SIZE] = {};
  uint64_t totalHashed = 0;
  uint64_t declaredSize = 0;
  uint64_t contentFed = 0;
  size_t blockUsed = 0;
};

constexpr size_t SHA_HEX_LENGTH = 2 * GitBlobHasher::SHA_SIZE;

// Parses exactly len characters; succeeds only for 40 hex digits.
bool shaFromHex(const char* hex, size_t len, uint8_t out[GitBlobHasher::SHA_SIZE]);

// Writes 40 lower-case hex characters and a terminating '\0'.
void shaToHex(const uint8_t sha[GitBlobHasher::SHA_SIZE], char out[SHA_HEX_LENGTH + 1]);
