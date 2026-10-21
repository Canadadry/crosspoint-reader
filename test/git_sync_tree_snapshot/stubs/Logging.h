#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

// Keeps every formatted line, so a test can check what was logged and that no path leaked.
namespace logging_stub {
inline std::vector<std::string>& lines() {
  static std::vector<std::string> captured;
  return captured;
}

inline void sink(const char* origin, const char* format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  lines().push_back(std::string(origin) + ": " + text);
}
}  // namespace logging_stub

#define LOG_ERR(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INF(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)
#define LOG_DBG(origin, format, ...) logging_stub::sink(origin, format __VA_OPT__(, ) __VA_ARGS__)
