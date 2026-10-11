#pragma once

#include <cstddef>

// Heap a whole sync may hold at once, TLS session excluded, and each part's share of it.
constexpr size_t SYNC_HEAP_BUDGET_BYTES = 65536;
constexpr size_t SYNC_PLANNER_SHARE_BYTES = 51200;
constexpr size_t SYNC_EXECUTOR_SHARE_BYTES = 3072;
// Run state, device context, owned strings, hash buffer and one transient request or store save.
constexpr size_t SYNC_GLUE_SHARE_BYTES = 11264;

static_assert(SYNC_GLUE_SHARE_BYTES == SYNC_HEAP_BUDGET_BYTES - SYNC_PLANNER_SHARE_BYTES - SYNC_EXECUTOR_SHARE_BYTES,
              "the glue share is what the planner and executor leave of SYNC_HEAP_BUDGET_BYTES");
