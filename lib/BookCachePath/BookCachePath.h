#pragma once

#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>

enum class BookCacheKind : uint8_t { Epub, Txt, Xtc };

inline constexpr const char* BOOK_CACHE_PREFIXES[] = {"epub_", "txt_", "xtc_"};

// std::hash width follows size_t, so host and device derive different keys for the same path.
inline size_t bookCacheKey(std::string_view absPath) { return std::hash<std::string_view>{}(absPath); }

// <cacheRoot>/<prefix><key>: the cache directory every reader and the sync use for a book.
inline std::string bookCacheDir(const std::string& cacheRoot, BookCacheKind kind, const std::string& absPath) {
  return cacheRoot + "/" + BOOK_CACHE_PREFIXES[static_cast<size_t>(kind)] + std::to_string(bookCacheKey(absPath));
}

// Same text as bookCacheDir, into a caller buffer. Refuses (empty out) when it does not fit.
inline bool formatBookCacheDir(char* out, size_t capacity, std::string_view cacheRoot, BookCacheKind kind,
                               std::string_view absPath) {
  if (out == nullptr || capacity == 0) {
    LOG_ERR("BCP", "No output buffer");
    return false;
  }
  const int written = snprintf(out, capacity, "%.*s/%s%zu", static_cast<int>(cacheRoot.size()), cacheRoot.data(),
                               BOOK_CACHE_PREFIXES[static_cast<size_t>(kind)], bookCacheKey(absPath));
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    LOG_ERR("BCP", "Cache dir does not fit in %u bytes", static_cast<unsigned>(capacity));
    out[0] = '\0';
    return false;
  }
  return true;
}
