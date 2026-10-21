#include "GitBlobHasher.h"

#include <cstring>

namespace {

constexpr uint32_t INITIAL_STATE[GitBlobHasher::STATE_WORDS] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                                                                0xC3D2E1F0};
constexpr size_t LENGTH_FIELD_SIZE = 8;
constexpr uint32_t ROUND_CONSTANTS[4] = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6};

constexpr int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

constexpr uint32_t rotl(uint32_t value, unsigned bits) { return (value << bits) | (value >> (32 - bits)); }

}  // namespace

void GitBlobHasher::begin(uint64_t size) {
  memcpy(state, INITIAL_STATE, sizeof(state));
  totalHashed = 0;
  blockUsed = 0;
  declaredSize = size;
  contentFed = 0;

  // 20 digits hold any uint64_t.
  char digits[20];
  size_t digitCount = 0;
  do {
    digits[digitCount++] = static_cast<char>('0' + size % 10);
    size /= 10;
  } while (size != 0);

  static constexpr char PREFIX[] = "blob ";
  constexpr size_t PREFIX_LEN = sizeof(PREFIX) - 1;
  uint8_t header[PREFIX_LEN + sizeof(digits) + 1];
  memcpy(header, PREFIX, PREFIX_LEN);
  size_t headerLen = PREFIX_LEN;
  while (digitCount > 0) header[headerLen++] = static_cast<uint8_t>(digits[--digitCount]);
  header[headerLen++] = 0;
  absorb(header, headerLen);
}

void GitBlobHasher::update(const uint8_t* data, size_t len) {
  contentFed += len;
  absorb(data, len);
}

bool GitBlobHasher::finish(uint8_t out[SHA_SIZE]) {
  if (contentFed != declaredSize) {
    memset(out, 0, SHA_SIZE);
    return false;
  }

  const uint64_t bitLength = totalHashed * 8;

  static constexpr uint8_t PADDING[BLOCK_SIZE] = {0x80};
  const size_t lengthOffset = BLOCK_SIZE - LENGTH_FIELD_SIZE;
  const size_t padLen = blockUsed < lengthOffset ? lengthOffset - blockUsed : BLOCK_SIZE + lengthOffset - blockUsed;
  absorb(PADDING, padLen);

  uint8_t lengthBytes[LENGTH_FIELD_SIZE];
  for (size_t i = 0; i < LENGTH_FIELD_SIZE; i++) {
    lengthBytes[i] = static_cast<uint8_t>(bitLength >> (8 * (LENGTH_FIELD_SIZE - 1 - i)));
  }
  absorb(lengthBytes, sizeof(lengthBytes));

  for (size_t i = 0; i < STATE_WORDS; i++) {
    out[4 * i] = static_cast<uint8_t>(state[i] >> 24);
    out[4 * i + 1] = static_cast<uint8_t>(state[i] >> 16);
    out[4 * i + 2] = static_cast<uint8_t>(state[i] >> 8);
    out[4 * i + 3] = static_cast<uint8_t>(state[i]);
  }
  return true;
}

void GitBlobHasher::absorb(const uint8_t* data, size_t len) {
  totalHashed += len;
  while (len > 0) {
    const size_t take = (BLOCK_SIZE - blockUsed < len) ? BLOCK_SIZE - blockUsed : len;
    memcpy(block + blockUsed, data, take);
    blockUsed += take;
    data += take;
    len -= take;
    if (blockUsed == BLOCK_SIZE) {
      compressBlock();
      blockUsed = 0;
    }
  }
}

void GitBlobHasher::compressBlock() {
  // 16-word rolling message schedule; words are assembled from bytes, so the block needs no alignment.
  uint32_t w[16];
  for (size_t i = 0; i < 16; i++) {
    w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) | (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
           (static_cast<uint32_t>(block[4 * i + 2]) << 8) | static_cast<uint32_t>(block[4 * i + 3]);
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];

  for (size_t t = 0; t < 80; t++) {
    if (t >= 16) {
      w[t & 15] = rotl(w[(t - 3) & 15] ^ w[(t - 8) & 15] ^ w[(t - 14) & 15] ^ w[t & 15], 1);
    }
    uint32_t f;
    if (t < 20) {
      f = (b & c) | (~b & d);
    } else if (t < 40 || t >= 60) {
      f = b ^ c ^ d;
    } else {
      f = (b & c) | (b & d) | (c & d);
    }
    const uint32_t temp = rotl(a, 5) + f + e + ROUND_CONSTANTS[t / 20] + w[t & 15];
    e = d;
    d = c;
    c = rotl(b, 30);
    b = a;
    a = temp;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}

void shaToHex(const uint8_t sha[GitBlobHasher::SHA_SIZE], char out[SHA_HEX_LENGTH + 1]) {
  static constexpr char DIGITS[] = "0123456789abcdef";
  for (size_t i = 0; i < GitBlobHasher::SHA_SIZE; i++) {
    out[2 * i] = DIGITS[sha[i] >> 4];
    out[2 * i + 1] = DIGITS[sha[i] & 0x0F];
  }
  out[SHA_HEX_LENGTH] = '\0';
}

bool shaFromHex(const char* hex, size_t len, uint8_t out[GitBlobHasher::SHA_SIZE]) {
  if (len != SHA_HEX_LENGTH) return false;
  for (size_t i = 0; i < GitBlobHasher::SHA_SIZE; i++) {
    const int high = hexNibble(hex[2 * i]);
    const int low = hexNibble(hex[2 * i + 1]);
    if (high < 0 || low < 0) return false;
    out[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}
