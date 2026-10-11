#include "ProtectedPath.h"

#include <algorithm>
#include <iterator>

namespace {
constexpr std::string_view PROTECTED_NAMES[] = {"System Volume Information", "XTCache"};

bool isProtectedName(std::string_view name) {
  if (!name.empty() && name.front() == '.') return true;
  return std::any_of(std::begin(PROTECTED_NAMES), std::end(PROTECTED_NAMES),
                     [name](std::string_view item) { return name == item; });
}
}  // namespace

bool isProtectedStoragePath(std::string_view path) {
  size_t start = 0;
  while (start < path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string_view::npos) end = path.size();
    if (isProtectedName(path.substr(start, end - start))) return true;
    start = end + 1;
  }
  return false;
}
