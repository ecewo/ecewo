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

#include "ecewo.h"
#include "request.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static const char *get_req(const request_t *request, const char *key, bool case_insensitive) {
  if (!request || !request->items || !key || request->count == 0)
    return NULL;

  int (*cmp)(const char *, const char *) = case_insensitive ? strcasecmp : strcmp;

  for (uint16_t i = 0; i < request->count; i++) {
    if (!request->items[i].key)
      continue;

    bool match = (cmp(request->items[i].key, key) == 0);

    if (match)
      return request->items[i].value;
  }

  return NULL;
}

const char *ecewo_get_param(const Req *req, const char *key) {
  if (!req)
    return NULL;

  return get_req(&req->params, key, false);
}

const char *ecewo_get_query(const Req *req, const char *key) {
  if (!req)
    return NULL;

  return get_req(&req->query, key, false);
}

const char *ecewo_get_header(const Req *req, const char *key) {
  if (!req)
    return NULL;

  return get_req(&req->headers, key, true);
}

void ecewo_set_context(Req *req, const char *key, void *data) {
  if (!req || !key)
    return;

  if (!req->ctx) {
    req->ctx = arena_alloc(req->arena, sizeof(context_t));
    if (!req->ctx)
      return;
    memset(req->ctx, 0, sizeof(context_t));
  }

  context_t *ctx = req->ctx;

  for (uint32_t i = 0; i < ctx->count; i++) {
    if (ctx->entries[i].key && strcmp(ctx->entries[i].key, key) == 0) {
      ctx->entries[i].data = data;
      return;
    }
  }

  if (ctx->count >= ctx->capacity) {
    uint32_t new_capacity = ctx->capacity == 0 ? 8 : ctx->capacity * 2;

    context_entry_t *new_entries = arena_realloc(
        req->arena,
        ctx->entries,
        ctx->capacity * sizeof(context_entry_t),
        new_capacity * sizeof(context_entry_t));

    if (!new_entries)
      return;

    memset(&new_entries[ctx->capacity], 0, (new_capacity - ctx->capacity) * sizeof(context_entry_t));

    ctx->entries = new_entries;
    ctx->capacity = new_capacity;
  }

  context_entry_t *entry = &ctx->entries[ctx->count];

  entry->key = arena_strdup(req->arena, key);
  if (!entry->key)
    return;

  entry->data = data;
  ctx->count++;
}

void *ecewo_get_context(Req *req, const char *key) {
  if (!req || !req->ctx || !key)
    return NULL;

  context_t *ctx = req->ctx;

  for (uint32_t i = 0; i < ctx->count; i++) {
    if (ctx->entries[i].key && strcmp(ctx->entries[i].key, key) == 0)
      return ctx->entries[i].data;
  }

  return NULL;
}
