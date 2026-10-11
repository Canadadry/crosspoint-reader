#pragma once

#include <HalStorage.h>
#include <SyncStorage.h>

// SyncStorage over HalStorage. SdFat's FAT/exFAT lookup already ignores letter case, so no folding is added
// here. The open-file rule (no rename or remove of an open file) is left to the executor's call order.
class HalSyncStorage final : public SyncStorage {
 public:
  HalSyncStorage() = default;
  ~HalSyncStorage() override;
  HalSyncStorage(const HalSyncStorage&) = delete;
  HalSyncStorage& operator=(const HalSyncStorage&) = delete;

  SyncFile* open(const char* path, SyncOpenMode mode) override;
  void close(SyncFile* file) override;

  bool exists(const char* path) override;
  bool fileSize(const char* path, uint64_t& out) override;
  bool isEmptyDirectory(const char* path) override;

  bool mkdirs(const char* path) override;
  bool remove(const char* path) override;
  bool rmdir(const char* path) override;
  bool rename(const char* from, const char* to) override;

 private:
  class Slot final : public SyncFile {
   public:
    int read(uint8_t* buf, size_t len) override;
    bool seek(uint32_t offset) override;
    bool write(const uint8_t* buf, size_t len) override;
    // HalFile::flush() reports nothing, so only a failed write() makes this false.
    bool flush() override;
    uint64_t size() override;

    void release();

    HalFile file;
    bool inUse = false;
    bool writeFailed = false;
  };

  static bool isDirectory(const char* path);

  Slot slots[MAX_OPEN_FILES];
};
