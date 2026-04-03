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
#include "uv.h"
#include "http.h"
#include "arena-internal.h"
#include "server.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifndef BODY_MAX_SIZE
#define BODY_MAX_SIZE (10UL * 1024UL * 1024UL) /* 10MB */
#endif

typedef struct {
  ecewo_request_t *req;
  ecewo_response_t *res;
  ecewo__client_t *client;
  BodyDataCb on_data;
  BodyEndCb on_end;
  size_t max_size;
  size_t bytes_received;
  bool streaming_enabled;
  bool completed;
  bool errored;
} StreamCtx;

static StreamCtx *get_ctx(ecewo_request_t *req) {
  return (StreamCtx *)ecewo_get_context(req, "_body_stream");
}

static StreamCtx *get_or_create_ctx(ecewo_request_t *req) {
  if (!req || !req->arena)
    return NULL;

  StreamCtx *ctx = get_ctx(req);
  if (ctx)
    return ctx;

  ctx = ecewo_alloc(req->arena, sizeof(StreamCtx));
  if (!ctx)
    return NULL;

  memset(ctx, 0, sizeof(StreamCtx));
  ctx->req = req;
  ctx->max_size = BODY_MAX_SIZE;

  if (req->ecewo__client_socket)
    ctx->client = (ecewo__client_t *)req->ecewo__client_socket->data;

  ecewo_set_context(req, "_body_stream", ctx);
  return ctx;
}

// body_chunk_cb_t implementation (called from on_body_cb in http.c)
// This is the function pointer stored in http_context_t->on_body_chunk
// It receives raw chunks as they arrive from the parser.
static int stream_on_chunk(void *udata, const uint8_t *data, size_t len) {
  StreamCtx *ctx = (StreamCtx *)udata;
  if (!ctx || !data || len == 0)
    return BODY_CHUNK_CONTINUE;

  if (ctx->max_size > 0 && ctx->bytes_received + len > ctx->max_size) {
    LOG_ERROR("Body size limit exceeded: received %zu, limit %zu. Use body_limit() to increase the limit.",
              ctx->bytes_received + len, ctx->max_size);

    return BODY_CHUNK_ERROR;
  }

  ctx->bytes_received += len;

  if (ctx->on_data)
    ctx->on_data(ctx->req, data, len);

  return BODY_CHUNK_CONTINUE;
}

void ecewo_body_stream(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  if (!req || !res || !next)
    return;

  StreamCtx *ctx = get_or_create_ctx(req);
  if (!ctx) {
    ecewo_send_text(res, INTERNAL_SERVER_ERROR, "Internal server error");
    return;
  }

  ctx->streaming_enabled = true;

  // Wire the chunk callback into the http context so on_body_cb
  // forwards chunks here instead of buffering them.
  if (ctx->client && ctx->client->parser_initialized) {
    http_context_t *hctx = &ctx->client->persistent_context;
    hctx->on_body_chunk = stream_on_chunk;
    hctx->stream_udata = ctx;
  }

  next(req, res);
}

void ecewo_body_on_data(ecewo_request_t *req, BodyDataCb callback) {
  if (!req || !callback)
    return;

  StreamCtx *ctx = get_or_create_ctx(req);
  if (!ctx)
    return;

  if (!ctx->streaming_enabled) {
    LOG_ERROR("body_on_data requires body_stream middleware");
    return;
  }

  ctx->on_data = callback;
}

void ecewo_body_on_end(ecewo_request_t *req, ecewo_response_t *res, BodyEndCb callback) {
  if (!req || !res || !callback)
    return;

  StreamCtx *ctx = get_or_create_ctx(req);
  if (!ctx)
    return;

  ctx->res = res;
  ctx->on_end = callback;

  // In buffered mode body_on_data already marked completed
  if (ctx->completed)
    callback(req, res);
}

size_t ecewo_body_limit(ecewo_request_t *req, size_t max_size) {
  if (!req)
    return 0;

  StreamCtx *ctx = get_or_create_ctx(req);
  if (!ctx)
    return 0;

  size_t prev = ctx->max_size;
  ctx->max_size = max_size == 0 ? BODY_MAX_SIZE : max_size;
  return prev;
}

// Called by router.c after full message received in streaming mode
void body_stream_complete(ecewo_request_t *req) {
  if (!req)
    return;

  StreamCtx *ctx = get_ctx(req);
  if (!ctx || ctx->completed)
    return;

  ctx->completed = true;

  if (ctx->on_end)
    ctx->on_end(req, ctx->res);
}
