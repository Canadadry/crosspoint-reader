#include "HalSyncStorage.h"

#include <Logging.h>
#include <SyncPathFold.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

bool refuseFatUnsafe(const char* path) {
  if (isFatSafeName(path, strlen(path))) return false;
  LOG_ERR("GSYNC", "Refused FAT-forbidden name: %s", path);
  return true;
}

}  // namespace

HalSyncStorage::~HalSyncStorage() {
  for (Slot& slot : slots) {
    if (slot.inUse) slot.release();
  }
}

SyncFile* HalSyncStorage::open(const char* path, SyncOpenMode mode) {
  Slot* const slot =
      std::find_if(std::begin(slots), std::end(slots), [](const Slot& candidate) { return !candidate.inUse; });
  if (slot == std::end(slots)) {
    LOG_ERR("GSYNC", "No free file slot for %s", path);
    return nullptr;
  }

  oflag_t flags = O_RDONLY;
  switch (mode) {
    case SyncOpenMode::Read:
      if (isDirectory(path)) return nullptr;
      break;
    case SyncOpenMode::Truncate:
      if (refuseFatUnsafe(path)) return nullptr;
      flags = O_WRONLY | O_CREAT | O_TRUNC;
      break;
    case SyncOpenMode::Append:
      flags = O_RDWR | O_APPEND;
      break;
  }

  slot->file = Storage.open(path, flags);
  if (!slot->file) {
    // Frees the HalFile::Impl that open() allocated even on failure.
    slot->file = HalFile();
    return nullptr;
  }
  slot->inUse = true;
  slot->writeFailed = false;
  return slot;
}

void HalSyncStorage::close(SyncFile* file) {
  Slot* const slot =
      std::find_if(std::begin(slots), std::end(slots), [file](const Slot& s) { return &s == file && s.inUse; });
  if (slot != std::end(slots)) slot->release();
}

bool HalSyncStorage::exists(const char* path) { return Storage.exists(path); }

bool HalSyncStorage::fileSize(const char* path, uint64_t& out) {
  HalFile file = Storage.open(path, O_RDONLY);
  if (!file || file.isDirectory()) return false;
  out = file.fileSize64();
  return true;
}

bool HalSyncStorage::isEmptyDirectory(const char* path) {
  HalFile dir = Storage.open(path, O_RDONLY);
  if (!dir || !dir.isDirectory()) return false;
  // openNextFile() skips the "." and ".." entries.
  HalFile entry = dir.openNextFile();
  return !entry;
}

bool HalSyncStorage::mkdirs(const char* path) {
  if (isDirectory(path)) return true;
  if (refuseFatUnsafe(path)) return false;
  return Storage.mkdir(path, true);
}

bool HalSyncStorage::remove(const char* path) {
  if (isDirectory(path)) return false;
  return Storage.remove(path);
}

bool HalSyncStorage::rmdir(const char* path) {
  if (!isEmptyDirectory(path)) return false;
  return Storage.rmdir(path);
}

bool HalSyncStorage::rename(const char* from, const char* to) {
  if (refuseFatUnsafe(to)) return false;
  if (Storage.exists(to)) {
    LOG_ERR("GSYNC", "Rename onto existing path refused: %s -> %s", from, to);
    return false;
  }
  return Storage.rename(from, to);
}

bool HalSyncStorage::isDirectory(const char* path) {
  HalFile file = Storage.open(path, O_RDONLY);
  return file && file.isDirectory();
}

int HalSyncStorage::Slot::read(uint8_t* buf, size_t len) { return file.read(buf, len); }

bool HalSyncStorage::Slot::seek(uint32_t offset) {
  if (offset > file.fileSize64()) return false;
  return file.seek(offset);
}

bool HalSyncStorage::Slot::write(const uint8_t* buf, size_t len) {
  if (len == 0) return true;
  if (file.write(buf, len) == len) return true;
  writeFailed = true;
  return false;
}

bool HalSyncStorage::Slot::flush() {
  file.flush();
  return !writeFailed;
}

uint64_t HalSyncStorage::Slot::size() { return file.fileSize64(); }

void HalSyncStorage::Slot::release() {
  file.close();
  // Frees the HalFile::Impl now rather than at the next open.
  file = HalFile();
  inUse = false;
  writeFailed = false;
}
