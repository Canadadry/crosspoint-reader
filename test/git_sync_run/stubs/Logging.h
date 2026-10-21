#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

// Keeps every formatted line with its level, so a test can check which errors a run logged.
namespace logging_stub {
struct Line {
  char level;
  std::string origin;
  std::string text;
};

inline std::vector<Line>& lines() {
  static std::vector<Line> captured;
  return captured;
}

inline void sink(char level, const char* origin, const char* format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  lines().push_back({level, origin, text});
}
}  // namespace logging_stub

#define LOG_ERR(origin, format, ...) logging_stub::sink('E', origin, format __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INF(origin, format, ...) logging_stub::sink('I', origin, format __VA_OPT__(, ) __VA_ARGS__)
#define LOG_DBG(origin, format, ...) logging_stub::sink('D', origin, format __VA_OPT__(, ) __VA_ARGS__)
