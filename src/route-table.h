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

#ifndef ECEWO_ROUTE_TABLE_H
#define ECEWO_ROUTE_TABLE_H

#include "ecewo.h"
#include "llhttp.h"

#ifndef MAX_INLINE_PARAMS
#define MAX_INLINE_PARAMS 8
#endif

typedef struct {
  const char *start;
  size_t len;
  bool is_param;
  bool is_wildcard;
} path_segment_t;

typedef struct {
  path_segment_t *segments;
  uint8_t count;
} tokenized_path_t;

typedef struct route_table route_table_t;

typedef struct {
  const char *data;
  size_t len;
} string_view_t;

typedef struct {
  string_view_t key;
  string_view_t value;
} param_match_t;

typedef struct {
  RequestHandler handler;
  void *middleware_ctx;
  param_match_t inline_params[MAX_INLINE_PARAMS];
  param_match_t *params;
  uint8_t param_count;
  uint8_t param_capacity;
} route_match_t;

bool route_table_match(route_table_t *table,
                       llhttp_t *parser,
                       const tokenized_path_t *tokenized_path,
                       route_match_t *match,
                       Arena *arena);

uint8_t route_table_allowed_methods(route_table_t *table,
                                    const tokenized_path_t *path);

int route_table_add(route_table_t *table,
                    llhttp_method_t method,
                    const char *path,
                    RequestHandler handler,
                    void *middleware_ctx);

int tokenize_path(Arena *arena, const char *path, size_t path_len, tokenized_path_t *result);
void route_table_free(route_table_t *table);
route_table_t *route_table_create(void);

#endif
