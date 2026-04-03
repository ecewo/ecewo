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

#ifndef ECEWO_REQUEST_H
#define ECEWO_REQUEST_H

#include <stddef.h>
#include <stdint.h>

struct ecewo__req_item_s {
  const char *key;
  const char *value;
};

struct ecewo__req_s {
  struct ecewo__req_item_s *items;
  uint16_t count;
  uint16_t capacity;
};

struct ecewo__res_header_s {
  const char *name;
  const char *value;
};

typedef struct {
  char *key;
  void *data;
} context_entry_t;

struct ecewo__req_ctx_s {
  context_entry_t *entries;
  uint32_t count;
  uint32_t capacity;
};

#endif
