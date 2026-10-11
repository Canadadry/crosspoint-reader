#pragma once

#include <cstddef>
#include <string_view>

// Copy of BookmarkUtil::getBookmarkPath (src/util/), which lib/ cannot include; a host test keeps them equal.
// Refuses (empty out) when the path does not fit.
bool formatBookmarkPath(char* out, size_t capacity, std::string_view absPath);
