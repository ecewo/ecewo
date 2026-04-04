// Copyright 2022 Alexey Kutepov <reximkut@gmail.com>
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

#ifndef ECEWO_ARENA_H
#define ECEWO_ARENA_H

#include <string.h>
#include "ecewo/export.h"

typedef struct ecewo__arena_region_s ecewo__arena_region_t;

typedef struct ecewo_arena_t {
  ecewo__arena_region_t *begin;
  ecewo__arena_region_t *end;
} ecewo_arena_t;

typedef struct {
  char *items;
  size_t count;
  size_t capacity;
} ecewo_string_builder_t;

ECEWO_EXPORT void *ecewo_alloc(ecewo_arena_t *arena, size_t size_bytes);
ECEWO_EXPORT void *ecewo_realloc(ecewo_arena_t *arena, void *oldptr, size_t oldsz, size_t newsz);
ECEWO_EXPORT char *ecewo_strdup(ecewo_arena_t *arena, const char *cstr);
ECEWO_EXPORT void *ecewo_memdup(ecewo_arena_t *arena, void *data, size_t size);
ECEWO_EXPORT char *ecewo_sprintf(ecewo_arena_t *arena, const char *format, ...);
ECEWO_EXPORT void ecewo_free(ecewo_arena_t *arena);

ECEWO_EXPORT ecewo_arena_t *ecewo_arena_borrow(void);
ECEWO_EXPORT void ecewo_arena_return(ecewo_arena_t *arena);

#ifdef ECEWO_DEBUG
ECEWO_EXPORT void ecewo_arena_pool_stats(void);
#endif

#ifndef ARENA_DA_INIT_CAP
#define ARENA_DA_INIT_CAP 256
#endif

#ifdef __cplusplus
#define ECEWO__CAST_PTR(ptr) (decltype(ptr))
#else
#define ECEWO__CAST_PTR(...)
#endif

#define ecewo_da_append(a, da, item)                                                      \
  do {                                                                                    \
    if ((da)->count >= (da)->capacity) {                                                  \
      size_t new_capacity = (da)->capacity == 0 ? ARENA_DA_INIT_CAP : (da)->capacity * 2; \
      (da)->items = ECEWO__CAST_PTR((da)->items) ecewo_realloc(                           \
          (a), (da)->items,                                                               \
          (da)->capacity * sizeof(*(da)->items),                                          \
          new_capacity * sizeof(*(da)->items));                                           \
      (da)->capacity = new_capacity;                                                      \
    }                                                                                     \
                                                                                          \
    (da)->items[(da)->count++] = (item);                                                  \
  } while (0)

#define ecewo_da_append_many(a, da, new_items, new_items_count)                               \
  do {                                                                                        \
    if ((da)->count + (new_items_count) > (da)->capacity) {                                   \
      size_t new_capacity = (da)->capacity;                                                   \
      if (new_capacity == 0)                                                                  \
        new_capacity = ARENA_DA_INIT_CAP;                                                     \
      while ((da)->count + (new_items_count) > new_capacity)                                  \
        new_capacity *= 2;                                                                    \
      (da)->items = ECEWO__CAST_PTR((da)->items) ecewo_realloc(                               \
          (a), (da)->items,                                                                   \
          (da)->capacity * sizeof(*(da)->items),                                              \
          new_capacity * sizeof(*(da)->items));                                               \
      (da)->capacity = new_capacity;                                                          \
    }                                                                                         \
    memcpy((da)->items + (da)->count, (new_items), (new_items_count) * sizeof(*(da)->items)); \
    (da)->count += (new_items_count);                                                         \
  } while (0)

#define ecewo_sb_append_cstr(a, sb, cstr) \
  do {                                    \
    const char *s = (cstr);               \
    size_t n = strlen(s);                 \
    ecewo_da_append_many(a, sb, s, n);    \
  } while (0)

#define ecewo_sb_append_null(a, sb) ecewo_da_append(a, sb, 0)

#endif
