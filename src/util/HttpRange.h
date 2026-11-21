#pragma once

#include <cstddef>
#include <cstdint>

// "bytes=" and the 20 digits of UINT64_MAX, '-', terminator.
constexpr size_t HTTP_RANGE_HEADER_CAPACITY = sizeof("bytes=") - 1 + 20 + sizeof("-");

// The Range header value asking for every byte from start on; false and out empty when it does not fit.
bool formatRangeHeader(char* out, size_t capacity, uint64_t start);

// Whether a 206's Content-Range value serves the body from exactly start.
bool contentRangeStartsAt(const char* value, uint64_t start);
