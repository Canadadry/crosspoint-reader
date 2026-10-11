#include "GitSyncConfigLoader.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

namespace git_sync_config {

namespace {

// Volatile stores so the wipe of a token-holding buffer is not dropped as dead before its release.
void wipe(void* data, size_t len) {
  volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
  while (len--) *p++ = 0;
}

// One byte past the cap tells a too-large file apart, plus the terminator readFileToBuffer writes.
constexpr size_t READ_BUFFER_BYTES = GitSyncConfig::MAX_CONFIG_FILE_BYTES + 2;

GitSyncConfigResult readAndParse(GitSyncConfig& out) {
  auto buf = makeUniqueNoThrow<char[]>(READ_BUFFER_BYTES);
  if (!buf) {
    return {GitSyncConfigStatus::OutOfMemory, GitSyncConfigField::None};
  }
  ScopedCleanup wipeBuf{[&buf] { wipe(buf.get(), READ_BUFFER_BYTES); }};

  const size_t n = Storage.readFileToBuffer(CONFIG_PATH, buf.get(), READ_BUFFER_BYTES);
  if (n == 0) {
    return {GitSyncConfigStatus::ReadError, GitSyncConfigField::None};
  }
  if (n > GitSyncConfig::MAX_CONFIG_FILE_BYTES) {
    return {GitSyncConfigStatus::TooLarge, GitSyncConfigField::None};
  }
  return parseGitSyncConfig(buf.get(), n, out);
}

}  // namespace

GitSyncConfigResult load(GitSyncConfig& out) {
  memset(&out, 0, sizeof(out));
  if (!Storage.exists(CONFIG_PATH)) {
    return {GitSyncConfigStatus::NotFound, GitSyncConfigField::None};
  }

  const GitSyncConfigResult result = readAndParse(out);
  if (result.status != GitSyncConfigStatus::Ok) {
    LOG_ERR("GSYNC", "%s: %s", CONFIG_PATH, describeGitSyncConfigResult(result));
    return result;
  }
  LOG_DBG("GSYNC", "Config loaded: %s/%s@%s", out.owner, out.repo, out.branch);
  return result;
}

bool exists() {
  auto config = makeUniqueNoThrow<GitSyncConfig>();
  if (!config) {
    LOG_ERR("GSYNC", "OOM: GitSyncConfig (%u bytes)", static_cast<unsigned>(sizeof(GitSyncConfig)));
    return false;
  }
  const bool ok = load(*config).status == GitSyncConfigStatus::Ok;
  wipe(config.get(), sizeof(GitSyncConfig));
  return ok;
}

}  // namespace git_sync_config
