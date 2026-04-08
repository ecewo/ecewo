// Copyright 2025-2026 Savas Sahin <savashn@proton.me>

// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "arena-internal.h"
#include "logger.h"
#include <stdlib.h>
#include <stdint.h>

#ifndef ARENA_POOL_CAP
#define ARENA_POOL_CAP 1024 /* Maximum allocatable arena count */
#endif

#ifndef PREALLOCATED_ARENA
#define PREALLOCATED_ARENA 32
#endif

#ifndef ARENA_POOL_LOW_WATERMARK
#define ARENA_POOL_LOW_WATERMARK 8 /* Grow when <= 8 available */
#endif

#ifndef ARENA_POOL_HIGH_WATERMARK
#define ARENA_POOL_HIGH_WATERMARK 64 /* Shrink when >= 64 available */
#endif

#ifndef ARENA_POOL_GROW_BATCH
#define ARENA_POOL_GROW_BATCH 8 /* Allocate 8 at a time */
#endif

typedef struct {
  ecewo_arena_t *arenas[ARENA_POOL_CAP];
  uint16_t head;
  uint16_t total_allocated;

#ifdef ECEWO_DEBUG
  uint16_t peak_usage;
  uint16_t grow_count;
  uint16_t shrink_count;
#endif
  bool initialized;
} arena_pool_t;

static arena_pool_t arena_pool = { 0 };

// Called when acquiring
static void arena_pool_try_grow(void) {
  if (arena_pool.head > ARENA_POOL_LOW_WATERMARK)
    return;

  uint16_t space_available = ARENA_POOL_CAP - arena_pool.head;
  if (space_available == 0)
    return;

  uint8_t to_allocate = ARENA_POOL_GROW_BATCH;
  if (to_allocate > space_available)
    to_allocate = space_available;

#ifdef ECEWO_DEBUG
  uint8_t allocated = 0;
#endif

  for (uint8_t i = 0; i < to_allocate; i++) {
    ecewo_arena_t *arena = malloc(sizeof(ecewo_arena_t));
    if (!arena)
      break;

    if (!new_region_to(&arena->begin, &arena->end, ARENA_REGION_SIZE)) {
      free(arena);
      break;
    }

    arena_pool.arenas[arena_pool.head++] = arena;
    arena_pool.total_allocated++;

#ifdef ECEWO_DEBUG
    allocated++;
  }

  if (allocated > 0) {
    arena_pool.grow_count++;
    LOG_DEBUG("Arena pool grew: +%d arenas (now %d/%d available)",
              allocated,
              arena_pool.head,
              ARENA_POOL_CAP);
#endif
  }
}

// Called when releasing
static void arena_pool_try_shrink(void) {
  if (arena_pool.head < ARENA_POOL_HIGH_WATERMARK)
    return;

  // Keep some reserve, don't shrink below initial size
  uint8_t target = PREALLOCATED_ARENA + ARENA_POOL_GROW_BATCH;
  if (arena_pool.head <= target)
    return;

  // Shrink by half of excess
  uint16_t excess = arena_pool.head - target;
  uint16_t to_free = excess / 2;
  if (to_free < ARENA_POOL_GROW_BATCH)
    to_free = ARENA_POOL_GROW_BATCH;

#ifdef ECEWO_DEBUG
  uint16_t freed = 0;
#endif

  while (to_free > 0 && arena_pool.head > target) {
    ecewo_arena_t *arena = arena_pool.arenas[--arena_pool.head];
    arena_pool.arenas[arena_pool.head] = NULL;

    if (arena) {
      ecewo_free(arena);
      free(arena);

#ifdef ECEWO_DEBUG
      freed++;
#endif
    }

    to_free--;
  }

#ifdef ECEWO_DEBUG
  if (freed > 0) {
    arena_pool.shrink_count++;
    LOG_DEBUG("Arena pool shrunk: -%d arenas (now %d/%d available)",
              freed, arena_pool.head, ARENA_POOL_CAP);
  }
#endif
}

static inline uint16_t get_arena_preallocation() {
  uint16_t preallocate = PREALLOCATED_ARENA;

  if (preallocate > ARENA_POOL_CAP) {
    LOG_DEBUG("%d exceeds maximum %d, capping to %d",
              preallocate, ARENA_POOL_CAP, ARENA_POOL_CAP);
    preallocate = ARENA_POOL_CAP;
  }

  const char *env_prealloc = getenv("ECEWO_ARENA_PREALLOC");
  if (!env_prealloc)
    return preallocate;

  char *endptr;
  long val = strtol(env_prealloc, &endptr, 10);

  if (endptr == env_prealloc || *endptr != '\0' || val <= 0 || val > UINT16_MAX) {
    LOG_DEBUG("Invalid ECEWO_ARENA_PREALLOC='%s', using default: %d",
              env_prealloc, preallocate);
    return preallocate;
  }

  uint16_t env_val = (uint16_t)val;

  if (env_val > ARENA_POOL_CAP) {
    LOG_DEBUG("ECEWO_ARENA_PREALLOC=%d exceeds maximum %d, capping to %d",
              env_val, ARENA_POOL_CAP, ARENA_POOL_CAP);
    return ARENA_POOL_CAP;
  } else {
    LOG_DEBUG("Using ECEWO_ARENA_PREALLOC=%d from environment", preallocate);
    return env_val;
  }
}

void arena_pool_init(void) {
  if (arena_pool.initialized)
    return;

  arena_pool.head = 0;
  arena_pool.total_allocated = 0;

#ifdef ECEWO_DEBUG
  arena_pool.peak_usage = 0;
  arena_pool.grow_count = 0;
  arena_pool.shrink_count = 0;
#endif

  const uint16_t preallocate = get_arena_preallocation();

// Pre-allocate arenas
#ifdef ECEWO_DEBUG
  uint16_t allocated = 0;
#endif

  for (uint16_t i = 0; i < preallocate; i++) {
    ecewo_arena_t *arena = malloc(sizeof(ecewo_arena_t));
    if (!arena) {
      LOG_DEBUG("Failed to allocate arena %d/%d, stopping pre-allocation",
                i + 1, preallocate);
      break;
    }

    // Pre-allocate first region
    if (!new_region_to(&arena->begin, &arena->end, ARENA_REGION_SIZE)) {
      free(arena);
      LOG_DEBUG("Failed to allocate region for arena %d/%d, stopping",
                i + 1, preallocate);
      break;
    }

    arena_pool.arenas[arena_pool.head++] = arena;
    arena_pool.total_allocated++;

#ifdef ECEWO_DEBUG
    allocated++;
#endif
  }

  arena_pool.initialized = true;

#ifdef ECEWO_DEBUG
  double allocated_mb = (allocated * ARENA_REGION_SIZE) / (1024.0 * 1024.0);
  LOG_DEBUG("Arena pool initialized: %d/%d arenas (%.2f MB)",
            allocated,
            ARENA_POOL_CAP,
            allocated_mb);
#endif
}

void arena_pool_destroy(void) {
  if (!arena_pool.initialized)
    return;

#ifdef ECEWO_DEBUG
  // Statistics before destruction
  if (arena_pool.grow_count > 0 || arena_pool.shrink_count > 0) {
    LOG_DEBUG("Arena pool statistics:");
    LOG_DEBUG("  Total allocated: %d arenas", arena_pool.total_allocated);
    LOG_DEBUG("  Peak usage: %d arenas", arena_pool.peak_usage);
    LOG_DEBUG("  Grow operations: %d", arena_pool.grow_count);
    LOG_DEBUG("  Shrink operations: %d", arena_pool.shrink_count);
  }
#endif

  for (int i = 0; i < arena_pool.head; i++) {
    if (arena_pool.arenas[i]) {
      ecewo_free(arena_pool.arenas[i]);
      free(arena_pool.arenas[i]);
      arena_pool.arenas[i] = NULL;
    }
  }

  arena_pool.head = 0;
  arena_pool.initialized = false;

  LOG_DEBUG("Arena pool destroyed");
}

ecewo_arena_t *ecewo_arena_borrow(void) {
  ecewo_arena_t *arena;

  if (arena_pool.head > 0) {
    // Take from pool
    arena = arena_pool.arenas[--arena_pool.head];
    arena_pool.arenas[arena_pool.head] = NULL;

#ifdef ECEWO_DEBUG
    int in_use = arena_pool.total_allocated - arena_pool.head;
    if (in_use > arena_pool.peak_usage)
      arena_pool.peak_usage = in_use;
#endif

    arena_pool_try_grow();
    arena_reset(arena);
    return arena;
  }

  // Pool is empty, check if we can grow
  if (arena_pool.total_allocated >= ARENA_POOL_CAP) {
    LOG_DEBUG("Arena pool exhausted! (max %d reached)", ARENA_POOL_CAP);
    return NULL;
  }

  // Allocate new arena
  arena = malloc(sizeof(ecewo_arena_t));
  if (!arena)
    return NULL;

  if (!new_region_to(&arena->begin, &arena->end, ARENA_REGION_SIZE)) {
    free(arena);
    return NULL;
  }

  arena_pool.total_allocated++;

#ifdef ECEWO_DEBUG
  int in_use = arena_pool.total_allocated - arena_pool.head;
  if (in_use > arena_pool.peak_usage)
    arena_pool.peak_usage = in_use;

  LOG_DEBUG("Arena pool: allocated new arena (total=%d/%d)",
            arena_pool.total_allocated, ARENA_POOL_CAP);
#endif

  return arena;
}

void ecewo_arena_return(ecewo_arena_t *arena) {
  if (!arena)
    return;

  if (!arena_pool.initialized) {
    ecewo_free(arena);
    free(arena);
    return;
  }

  // Keep only the first region, free the rest
  if (arena->begin && arena->begin->next) {
    ecewo__arena_region_t *to_free = arena->begin->next;
    arena->begin->next = NULL;

    while (to_free) {
      ecewo__arena_region_t *next = to_free->next;
      free(to_free);
      to_free = next;
    }
  }

  // Reset the first region
  if (arena->begin) {
    arena->begin->count = 0;
    arena->end = arena->begin;
  }

  if (arena_pool.head < ARENA_POOL_CAP) {
    arena_pool.arenas[arena_pool.head++] = arena;
    arena_pool_try_shrink();
  } else {
    // Pool is full, free directly
    ecewo_free(arena);
    free(arena);
  }
}

#ifdef ECEWO_DEBUG
void ecewo_arena_pool_stats(void) {
  if (!arena_pool.initialized) {
    LOG_DEBUG("Arena pool not initialized");
    return;
  }

  uint16_t available = arena_pool.head;
  uint16_t in_use = arena_pool.total_allocated - available;
  double available_mb = (available * ARENA_REGION_SIZE) / (1024.0 * 1024.0);
  double total_mb = (arena_pool.total_allocated * ARENA_REGION_SIZE) / (1024.0 * 1024.0);

  LOG_DEBUG("Arena Pool Statistics:");
  LOG_DEBUG("  Available: %d/%d arenas (%.2f MB)",
            available, arena_pool.total_allocated, available_mb);
  LOG_DEBUG("  In use: %d arenas", in_use);
  LOG_DEBUG("  Peak usage: %d arenas", arena_pool.peak_usage);
  LOG_DEBUG("  Total allocated: %.2f MB", total_mb);
  LOG_DEBUG("  Grow operations: %d", arena_pool.grow_count);
  LOG_DEBUG("  Shrink operations: %d", arena_pool.shrink_count);
}

#endif

bool arena_pool_is_initialized(void) {
  return arena_pool.initialized;
}
