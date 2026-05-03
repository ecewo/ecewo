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

#include "router.h"
#include "route-table.h"
#include "middleware.h"
#include "server.h"
#include "arena-internal.h"
#include "utils.h"
#include "request.h"
#include "logger.h"
#include <stdlib.h> // for strtol

extern void send_error(ecewo_arena_t *request_arena, uv_tcp_t *ecewo__client_socket, int error_code);
extern void body_stream_complete(ecewo_request_t *req);

// Extracts URL parameters from a previously matched route
static int extract_url_params(ecewo_arena_t *arena, const route_match_t *match, ecewo__req_t *url_params) {
  if (!arena || !match || !url_params)
    return -1;

  if (match->param_count == 0)
    return 0;

  url_params->capacity = match->param_count;
  url_params->count = match->param_count;
  url_params->items = ecewo_alloc(arena, sizeof(ecewo__req_item_t) * url_params->capacity);
  if (!url_params->items) {
    url_params->capacity = 0;
    url_params->count = 0;
    return -1;
  }

  const param_match_t *source = match->params ? match->params : match->inline_params;

  for (uint8_t i = 0; i < match->param_count; i++) {
    const string_view_t *key_sv = &source[i].key;
    const string_view_t *value_sv = &source[i].value;

    char *key = ecewo_alloc(arena, key_sv->len + 1);
    if (!key)
      return -1;
    memcpy(key, key_sv->data, key_sv->len);
    key[key_sv->len] = '\0';

    char *value = ecewo_alloc(arena, value_sv->len + 1);
    if (!value)
      return -1;
    memcpy(value, value_sv->data, value_sv->len);
    value[value_sv->len] = '\0';
    url_decode(value, false);

    url_params->items[i].key = key;
    url_params->items[i].value = value;
  }

  return 0;
}

static ecewo_request_t *create_req(ecewo_arena_t *request_arena, uv_tcp_t *ecewo__client_socket, ecewo__server_t *srv) {
  if (!request_arena)
    return NULL;

  ecewo_request_t *req = ecewo_alloc(request_arena, sizeof(ecewo_request_t));
  if (!req)
    return NULL;

  memset(req, 0, sizeof(ecewo_request_t));
  req->arena = request_arena;
  req->ecewo__client_socket = ecewo__client_socket;
  req->is_head_request = false;
  req->ctx = NULL;
  req->app = srv ? srv->app : NULL;

  return req;
}

static ecewo_response_t *create_res(ecewo_arena_t *request_arena, uv_tcp_t *ecewo__client_socket) {
  if (!request_arena)
    return NULL;

  ecewo_response_t *res = ecewo_alloc(request_arena, sizeof(ecewo_response_t));
  if (!res)
    return NULL;

  memset(res, 0, sizeof(ecewo_response_t));
  res->arena = request_arena;
  res->ecewo__client_socket = ecewo__client_socket;
  res->status = 200;
  res->content_type = ecewo_strdup(request_arena, "text/plain");
  res->keep_alive = 1;
  res->is_head_request = false;

  return res;
}

static int populate_req_from_context(ecewo_request_t *req, http_context_t *ctx, const char *path, size_t path_len) {
  if (!req || !ctx)
    return -1;

  if (ctx->method && ctx->method_length > 0) {
    req->method = ctx->method;
    req->is_head_request = (ctx->method_length == 4 && memcmp(ctx->method, "HEAD", 4) == 0);
  }

  if (path && path_len > 0)
    req->path = ctx->url;

  req->http_major = ctx->http_major;
  req->http_minor = ctx->http_minor;

  req->headers = &ctx->headers;
  req->query = &ctx->query_params;

  return 0;
}

// Empty handler for running global middleware only (OPTIONS preflight / CORS)
static void noop_route_handler(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  (void)res;
}

// Matches a route and invokes the handler/middleware chain.
static int dispatch(ecewo__server_t *srv,
                    ecewo_arena_t *arena,
                    uv_tcp_t *handle,
                    http_context_t *ctx,
                    ecewo_client_t *client,
                    const char *path,
                    size_t path_len,
                    ecewo_request_t **req_out,
                    ecewo_response_t **res_out) {
  ecewo_request_t *req = create_req(arena, handle, srv);
  ecewo_response_t *res = create_res(arena, handle);
  if (!req || !res) {
    send_error(arena, handle, 500);
    return -1;
  }

  res->keep_alive = ctx->keep_alive;
  res->is_head_request = (ctx->method_length == 4 && memcmp(ctx->method, "HEAD", 4) == 0);

  if (populate_req_from_context(req, ctx, path, path_len) != 0) {
    send_error(arena, handle, 500);
    return -1;
  }
  req->is_head_request = res->is_head_request;

  if (req_out)
    *req_out = req;
  if (res_out)
    *res_out = res;

  if (!srv || !srv->route_table || !ctx->method) {
    ecewo_header_set(res, "Content-Type", "text/plain");
    ecewo_send(res, 404, "404 Not Found", 13);
    return 0;
  }

  tokenized_path_t tok = { 0 };
  if (tokenize_path(arena, path, path_len, &tok) != 0) {
    send_error(arena, handle, 500);
    return -1;
  }

  route_match_t match;
  if (!route_table_match(srv->route_table, ctx->parser, &tok, &match, arena)) {
    // OPTIONS preflight: give global middleware a chance (e.g. CORS)
    if (ctx->method_length == 7 && memcmp(ctx->method, "OPTIONS", 7) == 0) {
      MiddlewareInfo dummy = { NULL, 0, noop_route_handler };
      chain_start(req, res, &dummy, srv);
      if (res->replied)
        return 0;
    }

    uint8_t allowed = route_table_allowed_methods(srv->route_table, &tok);
    if (allowed) {
      static const char *method_names[7] = {
        "DELETE", "GET", "HEAD", "POST", "PUT", "OPTIONS", "PATCH"
      };
      char allow_buf[64];
      size_t pos = 0;
      for (int i = 0; i < 7; i++) {
        if (allowed & (uint8_t)(1u << i)) {
          if (pos > 0) {
            allow_buf[pos++] = ',';
            allow_buf[pos++] = ' ';
          }
          size_t len = strlen(method_names[i]);
          memcpy(allow_buf + pos, method_names[i], len);
          pos += len;
        }
      }
      allow_buf[pos] = '\0';
      ecewo_header_set(res, "Allow", allow_buf);
      ecewo_header_set(res, "Content-Type", "text/plain");
      ecewo_send(res, 405, "405 Method Not Allowed", 22);
    } else {
      ecewo_header_set(res, "Content-Type", "text/plain");
      ecewo_send(res, 404, "404 Not Found", 13);
    }
    return 0;
  }

  if (match.param_count > 0) {
    ecewo__req_t *params = ecewo_alloc(arena, sizeof(ecewo__req_t));
    if (!params) {
      send_error(arena, handle, 500);
      return -1;
    }
    memset(params, 0, sizeof(ecewo__req_t));
    req->params = params;

    if (extract_url_params(arena, &match, req->params) != 0) {
      send_error(arena, handle, 500);
      return -1;
    }
  }

  if (!match.handler) {
    send_error(arena, handle, 500);
    return -1;
  }

  MiddlewareInfo *mw = (MiddlewareInfo *)match.middleware_ctx;

  bool has_stream_middleware = false;
  if (mw) {
    for (uint16_t i = 0; i < mw->middleware_count; i++) {
      if ((void *)mw->middleware[i] == (void *)ecewo_body_stream) {
        has_stream_middleware = true;
        break;
      }
    }
  }
  if (!has_stream_middleware && srv) {
    for (uint16_t i = 0; i < srv->global_middleware_count; i++) {
      if ((void *)srv->global_middleware[i].handler == (void *)ecewo_body_stream) {
        has_stream_middleware = true;
        break;
      }
    }
  }

  bool is_chunked = false;
  bool has_body = false;
  long content_length = 0;

  for (uint16_t i = 0; i < ctx->headers.count; i++) {
    const char *k = ctx->headers.items[i].key;
    const char *v = ctx->headers.items[i].value;

    if (strcasecmp(k, "Content-Length") == 0 && strcmp(v, "0") != 0) {
      has_body = true;
      char *endptr;
      content_length = strtol(v, &endptr, 10);
      if (endptr == v || *endptr != '\0')
        content_length = 0;
    }

    if (strcasecmp(k, "Transfer-Encoding") == 0) {
      has_body = true;
      is_chunked = true;
    }
  }

  if (!ctx->on_body_chunk && has_body && (content_length >= (long)BUFFERED_BODY_MAX_SIZE || is_chunked)) {
    ecewo_header_set(res, "Content-Type", "text/plain");
    res->keep_alive = false;
    ecewo_send(res, 413, "Payload Too Large", 17);
    return 0;
  }

  if (!has_stream_middleware && has_body && !ctx->message_complete) {
    if (client) {
      client->pending_handler = match.handler;
      client->pending_mw = (void *)mw;
      client->pending_req = req;
      client->pending_res = res;
      client->handler_pending = true;
    }
    return 0;
  }

  if (!has_stream_middleware && ctx->body_length > 0) {
    req->body = ctx->body;
    req->body_len = ctx->body_length;
  }

  if (mw)
    chain_start(req, res, mw, srv);
  else
    match.handler(req, res);

  return 0;
}

int router(ecewo_client_t *client, const char *request_data, size_t request_len) {
  if (!client || !request_data || request_len == 0) {
    if (client)
      send_error(NULL, (uv_tcp_t *)&client->handle, 400);
    return REQUEST_CLOSE;
  }

  ecewo_client_ref(client);

  ecewo__server_t *srv = client->srv;
  uv_tcp_t *handle = (uv_tcp_t *)&client->handle;
  http_context_t *ctx = &client->persistent_context;
  ecewo_arena_t *arena = client->connection_arena;

  int retval = REQUEST_CLOSE;

  if (uv_is_closing((uv_handle_t *)handle))
    goto done;

  parse_result_t result = http_parse_request(ctx, request_data, request_len);

  if (result == PARSE_PAUSED) {
    if (!arena) {
      send_error(NULL, handle, 500);
      goto done;
    }

    const char *path = ctx->url;
    size_t path_len = ctx->path_length;
    if (!path || path_len == 0) {
      path = "/";
      path_len = 1;
    }

    ecewo_request_t *req = NULL;
    ecewo_response_t *res = NULL;

    if (dispatch(srv, arena, handle, ctx, client, path, path_len, &req, &res) != 0)
      goto done;

    if (!client->valid)
      goto done;

    // If ecewo_body_stream middleware ran, save req/res so that when the body
    // finally arrives on a later TCP read, body_stream_complete can be called
    // on this req instead of dispatching a fresh one
    if (ctx->on_body_chunk && req) {
      client->stream_req = req;
      client->stream_res = res;
    }

    // Calculate remaining bytes before touching the parser
    // llhttp_get_error_pos() points to where the pause happened
    // Everything after that has not been parsed yet
    const char *pause_pos = llhttp_get_error_pos(ctx->parser);
    size_t consumed = pause_pos ? (size_t)(pause_pos - request_data) : request_len;
    size_t left = request_len > consumed ? request_len - consumed : 0;

    llhttp_resume(ctx->parser);

    if (res && res->replied) {
      if (client->taken_over)
        retval = REQUEST_PENDING;
      else
        retval = res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
      goto done;
    }

    parse_result_t body_result;
    if (left > 0)
      body_result = http_parse_request(ctx, pause_pos, left);
    else
      body_result = ctx->message_complete ? PARSE_SUCCESS : PARSE_INCOMPLETE;

    switch (body_result) {
    case PARSE_SUCCESS:
      if (ctx->on_body_chunk && req) {
        client->stream_req = NULL;
        client->stream_res = NULL;
        body_stream_complete(req);
      } else if (client->handler_pending) {
        client->handler_pending = false;
        ecewo_request_t *preq = client->pending_req;
        ecewo_response_t *pres = client->pending_res;
        if (preq && pres) {
          preq->body = ctx->body_length > 0 ? ctx->body : NULL;
          preq->body_len = ctx->body_length;
          MiddlewareInfo *pmw = (MiddlewareInfo *)client->pending_mw;
          if (pmw)
            chain_start(preq, pres, pmw, srv);
          else if (client->pending_handler)
            client->pending_handler(preq, pres);
        }
      }
      if (!client->valid)
        goto done;
      {
        ecewo_response_t *final_res = (client->pending_res && client->pending_res->replied)
            ? client->pending_res
            : res;
        if (final_res && !final_res->replied) {
          retval = REQUEST_PENDING;
        } else if (client->taken_over) {
          retval = REQUEST_PENDING;
        } else {
          retval = final_res && final_res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
        }
      }
      goto done;

    case PARSE_INCOMPLETE:
      retval = REQUEST_PENDING;
      goto done;

    case PARSE_OVERFLOW:
      LOG_ERROR("Body too large: %s", ctx->error_reason ? ctx->error_reason : "");
      send_error(arena, handle, 413);
      goto done;

    case PARSE_PAUSED:
    case PARSE_ERROR:
    default:
      LOG_ERROR("Parse error after resume: %s",
                ctx->error_reason ? ctx->error_reason : "unknown");
      send_error(arena, handle, 400);
      goto done;
    }
  }

  switch (result) {
  case PARSE_INCOMPLETE:
    retval = REQUEST_PENDING;
    goto done;

  case PARSE_OVERFLOW:
    LOG_ERROR("Request too large: %s", ctx->error_reason ? ctx->error_reason : "");
    send_error(NULL, handle, 413);
    goto done;

  case PARSE_ERROR:
    LOG_ERROR("Parse error: %s", ctx->error_reason ? ctx->error_reason : "unknown");
    send_error(NULL, handle, 400);
    goto done;

  case PARSE_SUCCESS:
    break;

  default:
    send_error(NULL, handle, 400);
    goto done;
  }

  // A streaming request whose body arrived across multiple TCP reads:
  // the first TCP read parsed headers and started dispatch but the body
  // was not yet complete (PARSE_INCOMPLETE), so body_stream_complete was
  // deferred. Call the complete cb on the saved req
  // instead of dispatching a fresh req/res
  if (ctx->on_body_chunk && client->stream_req) {
    ecewo_request_t *sreq = client->stream_req;
    ecewo_response_t *sres = client->stream_res;
    client->stream_req = NULL;
    client->stream_res = NULL;
    body_stream_complete(sreq);
    if (!client->valid)
      goto done;
    retval = (sres && !sres->replied)
        ? REQUEST_PENDING
        : (sres && sres->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE);
    goto done;
  }

  // PARSE_SUCCESS (EOF-terminated, no pause)
  if (!arena) {
    send_error(NULL, handle, 500);
    goto done;
  }

  if (http_message_needs_eof(ctx)) {
    if (http_finish_parsing(ctx) != PARSE_SUCCESS) {
      LOG_ERROR("Finish parse failed: %s", ctx->error_reason ? ctx->error_reason : "");
      send_error(arena, handle, 400);
      goto done;
    }
  }

  {
    const char *path = ctx->url;
    size_t path_len = ctx->path_length;
    if (!path || path_len == 0) {
      path = "/";
      path_len = 1;
    }

    ecewo_request_t *req = NULL;
    ecewo_response_t *res = NULL;

    if (dispatch(srv, arena, handle, ctx, client, path, path_len, &req, &res) != 0)
      goto done;

    if (!client->valid)
      goto done;

    if (res && !res->replied) {
      retval = REQUEST_PENDING;
      goto done;
    }

    /* If the handler took over the connection (e.g. WebSocket upgrade),
     * ecewo no longer owns the request lifecycle. Don't try to close or
     * keep-alive the connection from here. */
    if (client->taken_over) {
      retval = REQUEST_PENDING;
      goto done;
    }

    retval = res && res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
  }

done:
  ecewo_client_unref(client);
  return retval;
}
