#pragma once

#include <cstddef>
#include <cstdint>

// Shaped like HalFile::read and HalFile::seek, so the device adapter only forwards.
class RecordSource {
 public:
  virtual ~RecordSource() = default;
  // Bytes read: 0 at end of file, fewer than len near it, negative on I/O error.
  virtual int read(uint8_t* buf, size_t len) = 0;
  virtual bool seek(uint32_t offset) = 0;
};

class RecordSink {
 public:
  virtual ~RecordSink() = default;
  // True only when all len bytes were written.
  virtual bool write(const uint8_t* buf, size_t len) = 0;
};
