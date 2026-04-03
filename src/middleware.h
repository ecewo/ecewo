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

#ifndef ECEWO_MIDDLEWARE_H
#define ECEWO_MIDDLEWARE_H

#include "ecewo.h"
#include "llhttp.h"

#ifndef INITIAL_MW_CAPACITY
#define INITIAL_MW_CAPACITY 8
#endif

typedef struct MiddlewareInfo {
  ecewo__middleware_t *middleware;
  uint16_t middleware_count;
  ecewo__handler_t handler;
} MiddlewareInfo;

typedef struct {
  const char *path_prefix;
  ecewo__middleware_t handler;
} GlobalMiddlewareEntry;

void chain_start(ecewo_request_t *req, ecewo_response_t *res, MiddlewareInfo *middleware_info, ecewo__server_t *srv);
void reset_middleware(ecewo__server_t *srv);

#endif
