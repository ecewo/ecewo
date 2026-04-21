// Copyright 2026 Savas Sahin <savashn@proton.me>

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
//
// ---------------------------------------------------------------------------
// libFuzzer target for route registration (trie construction).
//
// The entire input is treated as a route pattern and registered under all
// seven HTTP methods on a fresh table, including a duplicate registration.
// The table is immediately freed so ASAN can detect leaks and UAF.
//
// Build alongside fuzz-router. See fuzz_router.c for build instructions.
// Run with:
//   mkdir -p fuzz/reg-corpus
//   ./build-fuzz/fuzz-route-register fuzz/reg-corpus \
//     -dict=fuzz/router.dict -max_len=512
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ecewo.h"
#include "route-table.h"

static void noop(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  (void)res;
}

static const llhttp_method_t methods[7] = {
  HTTP_DELETE,
  HTTP_GET,
  HTTP_HEAD,
  HTTP_POST,
  HTTP_PUT,
  HTTP_OPTIONS,
  HTTP_PATCH,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0 || size > 512)
    return 0;

  char pattern[513];
  memcpy(pattern, data, size);
  pattern[size] = '\0';

  route_table_t *table = route_table_create(NULL);
  if (!table)
    return 0;

  for (int m = 0; m < 7; m++) {
    route_table_add(table, NULL, methods[m], pattern, noop, NULL);
    // Duplicate registration must not double-free or leak.
    route_table_add(table, NULL, methods[m], pattern, noop, NULL);
  }

  route_table_free(table);
  return 0;
}
