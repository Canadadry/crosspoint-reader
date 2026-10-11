#pragma once

#include <cstddef>
#include <cstdint>

#include "GitBlobHasher.h"
#include "GitSyncConfig.h"

// Accept header of the trees call.
inline constexpr char GITHUB_ACCEPT_JSON[] = "application/vnd.github+json";
// Accept header of the blob call: the body is the raw file, not JSON or base64.
inline constexpr char GITHUB_ACCEPT_RAW[] = "application/vnd.github.raw+json";
// Basic auth username; GitHub ignores it and takes the token as the password.
inline constexpr char GITHUB_BASIC_AUTH_USER[] = "x";

// The trees URL with the longest owner, repo and branch, plus its terminator; the blob URL is shorter.
constexpr size_t GITHUB_API_URL_CAPACITY = sizeof("https://api.github.com/repos/") - 1 + GitSyncConfig::OWNER_MAX + 1 +
                                           GitSyncConfig::REPO_MAX + sizeof("/git/trees/") - 1 +
                                           GitSyncConfig::BRANCH_MAX + sizeof("?recursive=1");

// Both builders write the URL with its terminator into out. They return false and leave out empty when
// a field they use is empty or unterminated, or when the URL does not fit. Fields are not escaped: the
// config loader restricts them to URL-safe alphabets. Never log.

// https://api.github.com/repos/{owner}/{repo}/git/trees/{branch}?recursive=1
bool formatTreesUrl(char* out, size_t capacity, const GitSyncConfig& config);

// https://api.github.com/repos/{owner}/{repo}/git/blobs/{sha as 40 lower-case hex}
bool formatBlobUrl(char* out, size_t capacity, const GitSyncConfig& config, const uint8_t sha[GitBlobHasher::SHA_SIZE]);
