// Original work Copyright 2022 Alexey Kutepov <reximkut@gmail.com>
// Modified work Copyright 2025-2026 Savas Sahin <savashn@proton.me>

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

#ifndef ECEWO_ARENA_INTERNAL_H
#define ECEWO_ARENA_INTERNAL_H

#include "ecewo.h"

#ifndef ARENA_REGION_SIZE
#define ARENA_REGION_SIZE (64UL * 1024UL)
#endif

struct ecewo__arena_region_s {
  struct ecewo__arena_region_s *next;
  size_t count;
  size_t capacity;
  uintptr_t data[];
};

void arena_reset(ecewo_arena_t *a);
bool new_region_to(ecewo__arena_region_t **begin, ecewo__arena_region_t **end, size_t capacity);

void arena_pool_init(void);
void arena_pool_destroy(void);
bool arena_pool_is_initialized(void);

#endif
