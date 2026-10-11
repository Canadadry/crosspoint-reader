#pragma once

#include <climits>
#include <cstring>

#include "RecordStream.h"

// Record file in a caller-owned buffer. Writes append at the end; reads follow their own cursor.
class MemoryRecordStream : public RecordSource, public RecordSink {
 public:
  MemoryRecordStream(uint8_t* buffer, size_t capacity, size_t length = 0)
      : buffer(buffer), capacity(capacity), used(length < capacity ? length : capacity) {}

  int read(uint8_t* buf, size_t len) override {
    size_t count = used - cursor;
    if (count > len) count = len;
    if (count > INT_MAX) count = INT_MAX;
    memcpy(buf, buffer + cursor, count);
    cursor += count;
    return static_cast<int>(count);
  }

  bool seek(uint32_t offset) override {
    if (offset > used) return false;
    cursor = offset;
    return true;
  }

  // Writes nothing and fails when len does not fit.
  bool write(const uint8_t* buf, size_t len) override {
    if (len > capacity - used) return false;
    memcpy(buffer + used, buf, len);
    used += len;
    return true;
  }

  const uint8_t* data() const { return buffer; }
  size_t length() const { return used; }

 private:
  uint8_t* buffer;
  size_t capacity;
  size_t used;
  size_t cursor = 0;
};
