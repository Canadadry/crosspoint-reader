#pragma once
#include <string_view>

// True when any '/'-separated segment of `path` starts with '.' or names a system folder
// (System Volume Information, XTCache). "." and ".." segments count as protected, so an
// unnormalised path cannot climb out of a protected folder unseen.
bool isProtectedStoragePath(std::string_view path);
