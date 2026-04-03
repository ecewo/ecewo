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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "arena-internal.h"

static inline arena_region_t *new_region(size_t capacity) {
  size_t size_bytes = sizeof(arena_region_t) + sizeof(uintptr_t) * capacity;
  arena_region_t *r = (arena_region_t *)malloc(size_bytes);

  if (!r)
    return NULL;

  r->next = NULL;
  r->count = 0;
  r->capacity = capacity;
  return r;
}

static inline void free_region(arena_region_t *r) {
  free(r);
}

bool new_region_to(arena_region_t **begin, arena_region_t **end, size_t capacity) {
  arena_region_t *region = new_region(capacity);
  if (!region) {
    *end = NULL;
    return false;
  }

  *end = region;
  *begin = region;

  return true;
}

void *ecewo_alloc(ecewo_arena_t *arena, size_t size_bytes) {
  size_t size = (size_bytes + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);

  if (arena->end == NULL) {
    size_t capacity = ARENA_REGION_SIZE;

    if (capacity < size)
      capacity = size;

    if (!new_region_to(&arena->begin, &arena->end, capacity))
      return NULL;
  }

  while (arena->end->count + size > arena->end->capacity && arena->end->next != NULL) {
    arena->end = arena->end->next;
  }

  if (arena->end->count + size > arena->end->capacity) {
    size_t capacity = ARENA_REGION_SIZE;
    if (capacity < size)
      capacity = size;

    arena->end->next = new_region(capacity);
    if (!arena->end->next)
      return NULL;

    arena->end = arena->end->next;
  }

  void *result = &arena->end->data[arena->end->count];
  arena->end->count += size;
  return result;
}

void *ecewo_realloc(ecewo_arena_t *arena, void *oldptr, size_t oldsz, size_t newsz) {
  if (newsz <= oldsz)
    return oldptr;

  void *newptr = ecewo_alloc(arena, newsz);

  if (!newptr)
    return NULL;

  char *newptr_char = (char *)newptr;
  char *oldptr_char = (char *)oldptr;
  for (size_t i = 0; i < oldsz; ++i) {
    newptr_char[i] = oldptr_char[i];
  }
  return newptr;
}

static size_t arena_strlen(const char *s) {
  size_t n = 0;
  while (*s++)
    n++;
  return n;
}

// TODO: Remove it in v4
void *memcpy(void *dest, const void *src, size_t n) {
  char *d = dest;
  const char *s = src;

  for (; n; n--)
    *d++ = *s++;

  return dest;
}

char *ecewo_strdup(ecewo_arena_t *arena, const char *cstr) {
  if (!cstr)
    return NULL;

  size_t n = arena_strlen(cstr);
  char *dup = (char *)ecewo_alloc(arena, n + 1);

  if (!dup)
    return NULL;

  memcpy(dup, cstr, n);
  dup[n] = '\0';
  return dup;
}

void *ecewo_memdup(ecewo_arena_t *arena, void *data, size_t size) {
  if (!data || size == 0)
    return NULL;

  void *ptr = ecewo_alloc(arena, size);
  if (!ptr)
    return NULL;

  return memcpy(ptr, data, size);
}

char *ecewo_sprintf(ecewo_arena_t *arena, const char *format, ...) {
  va_list args, args_copy;
  va_start(args, format);
  va_copy(args_copy, args);
  int n = vsnprintf(NULL, 0, format, args_copy); // NOLINT(clang-analyzer-valist.Uninitialized)
  va_end(args_copy);

  if (n < 0) {
    va_end(args);
    return NULL;
  }

  char *result = (char *)ecewo_alloc(arena, n + 1);

  if (!result) {
    va_end(args);
    return NULL;
  }

  vsnprintf(result, n + 1, format, args);
  va_end(args);

  return result;
}

void ecewo_free(ecewo_arena_t *arena) {
  arena_region_t *r = arena->begin;
  while (r) {
    arena_region_t *r0 = r;
    r = r->next;
    free_region(r0);
  }
  arena->begin = NULL;
  arena->end = NULL;
}

void arena_reset(ecewo_arena_t *a) {
  if (!a || !a->begin)
    return;

  arena_region_t *region = a->begin;
  while (region) {
    region->count = 0;
    region = region->next;
  }

  a->end = a->begin;
}
