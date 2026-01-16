// TODO: Early return in router.c before buffering all the body.
// TODO: Use req->context instead of passing void *ctx parameters

#include "ecewo.h"
#include "uv.h"
#include "body.h"
#include "http.h"
#include "arena.h"
#include "server.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifndef BODY_DEFAULT_MAX_SIZE
#define BODY_DEFAULT_MAX_SIZE (1UL * 1024UL * 1024UL)  // 1MB
#endif

typedef struct BodyStreamCtx {
  Req *req;
  client_t *client;
  bool streaming_enabled;
  size_t max_size;
  size_t bytes_received;
  bool first_chunk;
  bool completed;
  bool errored;
  BodyDataCb on_data;
  BodyEndCb on_end;
  BodyErrorCb on_error;
  void *cb_ctx;
} BodyStreamCtx;

static const BodyStreamCtx *get_stream_ctx(const Req *req) {
  if (!req)
    return NULL;
  return (const BodyStreamCtx *)get_context(req, "_body_stream");
}

static BodyStreamCtx *get_stream_ctx_mut(Req *req) {
  if (!req)
    return NULL;
  return (BodyStreamCtx *)get_context(req, "_body_stream");
}

static BodyStreamCtx *get_or_create_stream_ctx(Req *req) {
  if (!req || !req->arena)
    return NULL;
  
  BodyStreamCtx *ctx = get_stream_ctx_mut(req);
  if (ctx)
    return ctx;
  
  ctx = arena_alloc(req->arena, sizeof(BodyStreamCtx));
  if (!ctx)
    return NULL;
  
  memset(ctx, 0, sizeof(BodyStreamCtx));
  ctx->req = req;
  ctx->streaming_enabled = false;
  ctx->max_size = BODY_DEFAULT_MAX_SIZE;
  ctx->first_chunk = true;
  ctx->completed = false;
  ctx->errored = false;
  
  if (req->client_socket)
    ctx->client = (client_t *)req->client_socket->data;
  
  set_context(req, "_body_stream", ctx);
  
  if (ctx->client && ctx->client->parser_initialized) {
    http_context_t *http_ctx = &ctx->client->persistent_context;
    http_ctx->body_stream_ctx = ctx;
  }
  
  return ctx;
}

const char *body_bytes(const Req *req) {
  if (!req)
    return NULL;
  
  BodyStreamCtx *ctx = get_stream_ctx(req);
  if (ctx && ctx->streaming_enabled)
    return NULL;
  
  return req->body;
}

size_t body_len(const Req *req) {
  if (!req)
    return 0;
  
  BodyStreamCtx *ctx = get_stream_ctx(req);
  if (ctx && ctx->streaming_enabled)
    return 0;
  
  return req->body_len;
}

void body_on_data(Req *req, BodyDataCb callback, void *ctx) {
  if (!req || !callback)
    return;
  
  BodyStreamCtx *sctx = get_or_create_stream_ctx(req);
  if (!sctx)
    return;
  
  sctx->streaming_enabled = true;
  sctx->on_data = callback;
  sctx->cb_ctx = ctx;
  
  if (sctx->client && sctx->client->parser_initialized) {
    http_context_t *http_ctx = &sctx->client->persistent_context;
    http_ctx->body_streaming_enabled = true;
    http_ctx->body_stream_ctx = sctx;
  }
  
  // If body already buffered (late registration), deliver it now
  if (req->body && req->body_len > 0 && sctx->bytes_received == 0) {
    sctx->bytes_received = req->body_len;
    sctx->first_chunk = false;
    callback(req, req->body, req->body_len, ctx);
  }
}

void body_on_end(Req *req, BodyEndCb callback, void *ctx) {
  if (!req)
    return;
  
  BodyStreamCtx *sctx = get_or_create_stream_ctx(req);
  if (!sctx)
    return;
  
  sctx->on_end = callback;
  if (!sctx->cb_ctx)
    sctx->cb_ctx = ctx;
  
  if (sctx->completed && callback)
    callback(req, sctx->cb_ctx);
}

void body_on_error(Req *req, BodyErrorCb callback, void *ctx) {
  if (!req)
    return;
  
  BodyStreamCtx *sctx = get_or_create_stream_ctx(req);
  if (!sctx)
    return;
  
  sctx->on_error = callback;
  if (!sctx->cb_ctx)
    sctx->cb_ctx = ctx;
}

void body_pause(Req *req) {
  if (!req)
    return;
  
  BodyStreamCtx *ctx = get_stream_ctx(req);
  if (!ctx)
    return;
  
  // Stop reading from socket (backpressure)
  if (req->client_socket && !uv_is_closing((uv_handle_t *)req->client_socket)) {
    int result = uv_read_stop((uv_stream_t *)req->client_socket);
    if (result == 0) {
      LOG_DEBUG("Body paused at %zu bytes (backpressure)", ctx->bytes_received);
    } else {
      LOG_ERROR("Failed to pause body: %s", uv_strerror(result));
    }
  }
  
  if (ctx->client && ctx->client->parser_initialized)
    ctx->client->persistent_context.body_paused = true;
}

void body_resume(Req *req) {
  if (!req)
    return;
  
  BodyStreamCtx *ctx = get_stream_ctx(req);
  if (!ctx)
    return;
  
  if (ctx->client && ctx->client->parser_initialized) {
    http_context_t *http_ctx = &ctx->client->persistent_context;
    http_ctx->body_paused = false;
    
    if (llhttp_get_errno(http_ctx->parser) == HPE_PAUSED) {
      llhttp_resume(http_ctx->parser);
      LOG_DEBUG("Parser resumed");
    }
  }
  
  if (ctx->client && !ctx->client->closing) {
    resume_client_read(ctx->client);
    LOG_DEBUG("Body resumed");
  }
}

size_t body_limit(Req *req, size_t max_size) {
  if (!req)
    return 0;
  
  BodyStreamCtx *ctx = get_or_create_stream_ctx(req);
  if (!ctx)
    return 0;
  
  size_t prev = ctx->max_size;
  ctx->max_size = (max_size == 0) ? BODY_DEFAULT_MAX_SIZE : max_size;
  return prev;
}

// Called by http.c (on_body_cb) when body data arrives
// Returns: 0 = continue, 1 = pause (backpressure), -1 = error
int body_stream_on_chunk(void *stream_ctx, const char *data, size_t len) {
  BodyStreamCtx *ctx = (BodyStreamCtx *)stream_ctx;
  if (!ctx || !data || len == 0)
    return 0;
  
  if (ctx->max_size > 0 && ctx->bytes_received + len > ctx->max_size) {
    ctx->errored = true;
    
    if (ctx->on_error)
      ctx->on_error(ctx->req, "Body exceeds size limit", ctx->cb_ctx);
    
    return -1;
  }
  
  ctx->bytes_received += len;
  
  if (ctx->streaming_enabled && ctx->on_data) {
    bool continue_reading = ctx->on_data(ctx->req, data, len, ctx->cb_ctx);
    ctx->first_chunk = false;
    
    if (!continue_reading)
      return 1;
  }
  
  return 0;
}

// Called from router.c after request fully parsed
void body_on_complete(Req *req) {
  if (!req)
    return;
  
  BodyStreamCtx *ctx = get_stream_ctx(req);
  if (!ctx)
    return;
  
  ctx->completed = true;
  
  if (ctx->on_end)
    ctx->on_end(req, ctx->cb_ctx);
}

// Called from router.c if parse error occurs
void body_on_error_internal(Req *req, const char *error) {
  if (!req)
    return;
  
  BodyStreamCtx *ctx = get_stream_ctx(req);
  if (!ctx)
    return;
  
  ctx->errored = true;
  
  if (ctx->on_error)
    ctx->on_error(req, error, ctx->cb_ctx);
}

// It might be useful in future
// Check out the fn call in router
// void body_ctx_init(Req *req) {
//   (void)req;
// }


// Examples:
//
//   // Buffered mode (default)
//   void handler(Req *req, Res *res) {
//     const char *body = body_bytes(req);
//     size_t len = body_len(req);
//     send_json(res, OK, body);
//   }
//
// // Streaming mode
// void handler(Req *req, Res *res) {
//   set_context(req, "_res", res);
//   body_on_data(req, on_chunk, my_ctx);
//   body_on_end(req, on_complete, my_ctx);
// }

// bool on_chunk(Req *req, const char *data, size_t len, void *ctx) {
//   write_to_disk(data, len);
//   return true; // or false to pause
// }

// void on_complete(Req *req, void *ctx) {
//   Res *res = get_context(req, "_res");
//   send_text(res, OK, "Upload complete");
// }
