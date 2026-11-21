#include "HttpRange.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

bool formatRangeHeader(char* out, size_t capacity, uint64_t start) {
  if (!out || capacity == 0) return false;
  const int written = snprintf(out, capacity, "bytes=%" PRIu64 "-", start);
  if (written >= 0 && static_cast<size_t>(written) < capacity) return true;
  out[0] = '\0';
  return false;
}

bool contentRangeStartsAt(const char* value, uint64_t start) {
  static constexpr char UNIT[] = "bytes ";
  if (!value || strncmp(value, UNIT, sizeof(UNIT) - 1) != 0) return false;
  const char* p = value + sizeof(UNIT) - 1;
  if (*p < '0' || *p > '9') return false;
  uint64_t first = 0;
  for (; *p >= '0' && *p <= '9'; ++p) {
    const uint64_t digit = static_cast<uint64_t>(*p - '0');
    if (first > (UINT64_MAX - digit) / 10) return false;
    first = first * 10 + digit;
  }
  return *p == '-' && first == start;
}
