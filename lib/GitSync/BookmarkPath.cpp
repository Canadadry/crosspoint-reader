#include "BookmarkPath.h"

#include <Logging.h>

#include <cstring>

namespace {
constexpr char BOOKMARKS_DIR[] = "/.crosspoint/bookmarks/";
constexpr char BOOKMARK_EXTENSION[] = ".json";
}  // namespace

bool formatBookmarkPath(char* out, size_t capacity, std::string_view absPath) {
  if (out == nullptr || capacity == 0) {
    LOG_ERR("BMP", "No output buffer");
    return false;
  }
  // The reader drops the first byte unconditionally: it is the leading '/'.
  const std::string_view name = absPath.empty() ? absPath : absPath.substr(1);
  const size_t lastDot = name.find_last_of('.');
  const size_t nameLen = lastDot == std::string_view::npos ? name.size() : lastDot;
  const size_t dirLen = sizeof(BOOKMARKS_DIR) - 1;
  const size_t extLen = sizeof(BOOKMARK_EXTENSION) - 1;
  if (dirLen + nameLen + extLen >= capacity) {
    LOG_ERR("BMP", "Bookmark path does not fit in %u bytes", static_cast<unsigned>(capacity));
    out[0] = '\0';
    return false;
  }
  memcpy(out, BOOKMARKS_DIR, dirLen);
  char* cursor = out + dirLen;
  for (size_t i = 0; i < nameLen; i++) {
    const char c = name[i];
    *cursor++ = (c == '/' || c == '\\') ? '_' : c;
  }
  memcpy(cursor, BOOKMARK_EXTENSION, extLen + 1);
  return true;
}
