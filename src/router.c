#include "router.h"
#include "route-trie.h"
#include "middleware.h"
#include "server.h"
#include "arena.h"
#include "utils.h"
#include "request.h"
#include "body.h"
#include "logger.h"

extern void send_error(Arena *request_arena, uv_tcp_t *client_socket, int error_code);

// Extracts URL parameters from a previously matched route
// Example: From route /users/:id matched with /users/123, extracts parameter id=123
static int extract_url_params(Arena *arena, const route_match_t *match, request_t *url_params) {
  if (!arena || !match || !url_params)
    return -1;

  if (match->param_count == 0)
    return 0;

  url_params->capacity = match->param_count;
  url_params->count = match->param_count;
  url_params->items = arena_alloc(arena, sizeof(request_item_t) * url_params->capacity);
  if (!url_params->items) {
    url_params->capacity = 0;
    url_params->count = 0;
    return -1;
  }

  const param_match_t *source = match->params ? match->params : match->inline_params;

  for (uint8_t i = 0; i < match->param_count; i++) {
    const string_view_t *key_sv = &source[i].key;
    const string_view_t *value_sv = &source[i].value;

    char *key = arena_alloc(arena, key_sv->len + 1);
    if (!key)
      return -1;
    arena_memcpy(key, key_sv->data, key_sv->len);
    key[key_sv->len] = '\0';

    char *value = arena_alloc(arena, value_sv->len + 1);
    if (!value)
      return -1;
    arena_memcpy(value, value_sv->data, value_sv->len);
    value[value_sv->len] = '\0';

    url_params->items[i].key = key;
    url_params->items[i].value = value;
  }

  return 0;
}

static Req *create_req(Arena *request_arena, uv_tcp_t *client_socket) {
  if (!request_arena)
    return NULL;

  Req *req = arena_alloc(request_arena, sizeof(Req));
  if (!req)
    return NULL;

  memset(req, 0, sizeof(Req));
  req->arena = request_arena;
  req->client_socket = client_socket;
  req->is_head_request = false;

  req->ctx = arena_alloc(request_arena, sizeof(context_t));
  if (req->ctx)
    memset(req->ctx, 0, sizeof(context_t));

  // TODO: Something like body_ctx_init_fn might be useful
  // for implementing hooks like fastify has
  // but I'm not sure about that
  // so I'll just leave a comment line
  // body_ctx_init(req);

  return req;
}

static Res *create_res(Arena *request_arena, uv_tcp_t *client_socket) {
  if (!request_arena)
    return NULL;

  Res *res = arena_alloc(request_arena, sizeof(Res));
  if (!res)
    return NULL;

  memset(res, 0, sizeof(Res));
  res->arena = request_arena;
  res->client_socket = client_socket;
  res->status = 200;
  res->content_type = arena_strdup(request_arena, "text/plain");
  res->keep_alive = 1;
  res->is_head_request = false;

  return res;
}

static int populate_req_from_context(Req *req, http_context_t *ctx, const char *path, size_t path_len) {
  if (!req || !ctx)
    return -1;

  Arena *arena = req->arena;

  if (ctx->method && ctx->method_length > 0) {
    req->method = arena_alloc(arena, ctx->method_length + 1);
    if (!req->method)
      return -1;
    arena_memcpy(req->method, ctx->method, ctx->method_length);
    req->method[ctx->method_length] = '\0';

    req->is_head_request = (ctx->method_length == 4 && memcmp(ctx->method, "HEAD", 4) == 0);
  }

  if (path && path_len > 0) {
    req->path = arena_alloc(arena, path_len + 1);
    if (!req->path)
      return -1;
    arena_memcpy(req->path, path, path_len);
    req->path[path_len] = '\0';
  }

  if (ctx->body && ctx->body_length > 0) {
    req->body = arena_alloc(arena, ctx->body_length + 1);
    if (!req->body)
      return -1;
    arena_memcpy(req->body, ctx->body, ctx->body_length);
    req->body[ctx->body_length] = '\0';
    req->body_len = ctx->body_length;
  }

  req->http_major = ctx->http_major;
  req->http_minor = ctx->http_minor;

  req->headers = ctx->headers;
  req->query = ctx->query_params;

  return 0;
}

// Empty handler for running global middleware only
// it is needed for OPTIONS preflight CORS
static void noop_route_handler(Req *req, Res *res) {
  (void)req;
  (void)res;
}

int router(client_t *client, const char *request_data, size_t request_len) {
  uv_tcp_t *handle = (uv_tcp_t *)&client->handle;
  http_context_t *persistent_ctx = &client->persistent_context;

  if (!client || !request_data || request_len == 0) {
    if (client && client->handle.data)
      send_error(NULL, handle, 400);
    return REQUEST_CLOSE;
  }

  if (uv_is_closing((uv_handle_t *)&client->handle))
    return REQUEST_CLOSE;

  parse_result_t parse_result = http_parse_request(persistent_ctx, request_data, request_len);

  switch (parse_result) {
  case PARSE_SUCCESS:
    break;

  case PARSE_INCOMPLETE:
    if (persistent_ctx->body_paused) {
      LOG_DEBUG("Body paused - backpressure applied");
      return REQUEST_PENDING;
    }
    LOG_DEBUG("HTTP parsing incomplete - waiting for more data");
    return REQUEST_PENDING;

  case PARSE_OVERFLOW:
    LOG_ERROR("HTTP parsing failed: size limits exceeded");
    if (persistent_ctx->error_reason)
      LOG_ERROR(" - %s", persistent_ctx->error_reason);
    send_error(NULL, handle, 413);
    return REQUEST_CLOSE;

  case PARSE_ERROR:
  default:
    LOG_ERROR("HTTP parsing failed: %s", parse_result_to_string(parse_result));
    if (persistent_ctx->error_reason)
      LOG_ERROR(" - %s", persistent_ctx->error_reason);
    send_error(NULL, handle, 400);
    return REQUEST_CLOSE;
  }

  Arena *request_arena = client->connection_arena;

  if (!request_arena) {
    send_error(NULL, handle, 500);
    return REQUEST_CLOSE;
  }

  Req *req = create_req(request_arena, handle);
  Res *res = create_res(request_arena, handle);

  if (!req || !res) {
    send_error(request_arena, handle, 500);
    return REQUEST_CLOSE;
  }

  if (http_message_needs_eof(persistent_ctx)) {
    parse_result_t finish_result = http_finish_parsing(persistent_ctx);
    if (finish_result != PARSE_SUCCESS) {
      LOG_ERROR("HTTP finish parsing failed: %s", parse_result_to_string(finish_result));

      if (persistent_ctx->error_reason)
        LOG_ERROR(" - %s", persistent_ctx->error_reason);

      body_on_error_internal(req, persistent_ctx->error_reason ? 
                             persistent_ctx->error_reason : "Parse error");

      send_error(request_arena, handle, 400);
      return REQUEST_CLOSE;
    }
  }

  res->keep_alive = persistent_ctx->keep_alive;

  const char *path = persistent_ctx->url;
  size_t path_len = persistent_ctx->path_length;

  if (!path || path_len == 0) {
    path = "/";
    path_len = 1;
  }

  if (!global_route_trie || !persistent_ctx->method) {
    LOG_DEBUG("Missing route trie (%p) or method (%s)",
              (void *)global_route_trie,
              persistent_ctx->method ? persistent_ctx->method : "NULL");

    // 404 but still success response
    bool keep_alive = res->keep_alive;
    const char *not_found_msg = "404 Not Found";
    set_header(res, "Content-Type", "text/plain");
    reply(res, 404, not_found_msg, strlen(not_found_msg));
    return keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
  }

  tokenized_path_t tokenized_path = { 0 };
  if (tokenize_path(request_arena, path, path_len, &tokenized_path) != 0) {
    send_error(request_arena, handle, 500);
    return REQUEST_CLOSE;
  }

  if (populate_req_from_context(req, persistent_ctx, path, path_len) != 0) {
    send_error(request_arena, handle, 500);
    return REQUEST_CLOSE;
  }

  res->is_head_request = req->is_head_request;

  route_match_t match;
  if (!route_trie_match(global_route_trie,
                        persistent_ctx->parser,
                        &tokenized_path,
                        &match,
                        request_arena)) {

    LOG_DEBUG("Route not found: %s %s", persistent_ctx->method, path);

    // If this is an OPTIONS preflight, run the global middleware
    // so middleware like CORS can reply without requiring an OPTIONS route
    if (persistent_ctx->method_length == 7 && memcmp(persistent_ctx->method, "OPTIONS", 7) == 0) {
      // Create a small dummy MiddlewareInfo with a no-op handler.
      MiddlewareInfo dummy_mw;
      dummy_mw.middleware = NULL;
      dummy_mw.middleware_count = 0;
      dummy_mw.handler = noop_route_handler;

      chain_start(req, res, &dummy_mw);

      if (res->replied)
        return res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
    }

    bool keep_alive = res->keep_alive;
    set_header(res, "Content-Type", "text/plain");
    reply(res, 404, "404 Not Found", 13);
    return keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
  }

  if (extract_url_params(request_arena, &match, &req->params) != 0) {
    send_error(request_arena, handle, 500);
    return REQUEST_CLOSE;
  }

  if (!match.handler) {
    send_error(request_arena, handle, 500);
    return REQUEST_CLOSE;
  }

  MiddlewareInfo *middleware_info = (MiddlewareInfo *)match.middleware_ctx;

  if (!middleware_info) {
    LOG_DEBUG("No middleware info");
    match.handler(req, res);
    
    // Notify body completion if streaming was used
    if (persistent_ctx->body_stream_ctx)
      body_on_complete(req);
    
    if (!res->replied)
      return REQUEST_PENDING;
    
    return res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
  }

  chain_start(req, res, middleware_info);
  
  // Notify body completion if streaming was used
  if (persistent_ctx->body_stream_ctx)
    body_on_complete(req);
  
  if (!res->replied)
    return REQUEST_PENDING;

  return res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
}
