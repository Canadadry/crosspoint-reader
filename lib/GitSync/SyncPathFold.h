#pragma once

#include <cstddef>
#include <cstdint>

// FAT path identity: two paths name the same file on the card when they are equal after SdFat's toUpcase.
// UTF-8 is decoded strictly; an invalid byte and a code point above U+FFFF compare as themselves, unfolded.

bool syncPathsEqualFolded(const char* a, size_t aLen, const char* b, size_t bLen);

// FNV-1a of the folded code points: equal whenever syncPathsEqualFolded holds.
uint32_t syncPathKey(const char* path, size_t len);

// False if the path holds a FAT-forbidden printable character: : * ? " < > | or backslash.
bool isFatSafeName(const char* path, size_t len);
