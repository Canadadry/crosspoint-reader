#include "GitHubApi.h"

#include <cstdio>
#include <cstring>

namespace {

bool isFilled(const char* field, size_t size) {
  const size_t len = strnlen(field, size);
  return len > 0 && len < size;
}

// written is snprintf's result, or -1 when nothing was formatted.
bool finishUrl(char* out, size_t capacity, int written) {
  if (written >= 0 && static_cast<size_t>(written) < capacity) return true;
  if (out && capacity > 0) out[0] = '\0';
  return false;
}

bool canFormat(const char* out, size_t capacity, const GitSyncConfig& config) {
  return out && capacity > 0 && isFilled(config.owner, sizeof(config.owner)) &&
         isFilled(config.repo, sizeof(config.repo));
}

bool isUnreserved(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
         c == '_' || c == '~';
}

}  // namespace

bool formatTreesUrl(char* out, size_t capacity, const GitSyncConfig& config) {
  if (!canFormat(out, capacity, config) || !isFilled(config.branch, sizeof(config.branch))) {
    return finishUrl(out, capacity, -1);
  }
  const int written = snprintf(out, capacity, "https://api.github.com/repos/%s/%s/git/trees/%s?recursive=1",
                               config.owner, config.repo, config.branch);
  return finishUrl(out, capacity, written);
}

bool formatBlobUrl(char* out, size_t capacity, const GitSyncConfig& config,
                   const uint8_t sha[GitBlobHasher::SHA_SIZE]) {
  if (!canFormat(out, capacity, config) || !sha) return finishUrl(out, capacity, -1);
  char hex[SHA_HEX_LENGTH + 1];
  shaToHex(sha, hex);
  const int written =
      snprintf(out, capacity, "https://api.github.com/repos/%s/%s/git/blobs/%s", config.owner, config.repo, hex);
  return finishUrl(out, capacity, written);
}

bool formatRawUrl(char* out, size_t capacity, const GitSyncConfig& config, const char* repoPath) {
  if (!canFormat(out, capacity, config) || !isFilled(config.branch, sizeof(config.branch)) || !repoPath ||
      repoPath[0] == '\0') {
    return finishUrl(out, capacity, -1);
  }
  const int written = snprintf(out, capacity, "https://raw.githubusercontent.com/%s/%s/%s/", config.owner,
                               config.repo, config.branch);
  if (!finishUrl(out, capacity, written)) return false;
  static constexpr char HEX[] = "0123456789ABCDEF";
  size_t at = static_cast<size_t>(written);
  for (const char* p = repoPath; *p != '\0'; ++p) {
    const auto c = static_cast<unsigned char>(*p);
    const size_t need = (isUnreserved(c) || c == '/') ? 1 : 3;
    if (at + need >= capacity) return finishUrl(out, capacity, -1);
    if (need == 1) {
      out[at++] = static_cast<char>(c);
    } else {
      out[at++] = '%';
      out[at++] = HEX[c >> 4];
      out[at++] = HEX[c & 0x0F];
    }
  }
  out[at] = '\0';
  return true;
}
