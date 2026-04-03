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

#ifndef ECEWO_H
#define ECEWO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ecewo/arena.h"

typedef struct uv_loop_s uv_loop_t;
typedef struct uv_timer_s uv_timer_t;
typedef struct uv_tcp_s uv_tcp_t;
typedef uv_timer_t ecewo_timer_t;

typedef struct ecewo__client_s ecewo__client_t;
typedef struct ecewo__server_s ecewo__server_t;
typedef struct ecewo__req_item_s ecewo__req_item_t;
typedef struct ecewo__req_s ecewo__req_t;
typedef struct ecewo__req_ctx_s ecewo__req_ctx_t;
typedef struct ecewo__res_header_s ecewo__res_header_t;

typedef struct ecewo_app_t {
  ecewo__server_t *server;
  ecewo_arena_t *arena;
  int max_connections; // default: 10000
  int listen_backlog; // default: 511
  uint64_t idle_timeout_ms; // default: 60000
  uint64_t request_timeout_ms; // default: 0 (disabled)
  uint64_t cleanup_interval_ms; // default: 30000
  uint64_t shutdown_timeout_ms; // default: 15000
} ecewo_app_t;

typedef struct ecewo_request_t {
  ecewo_app_t *app;
  ecewo_arena_t *arena;
  uv_tcp_t *ecewo__client_socket;
  char *method;
  char *path;
  uint8_t *body;
  size_t body_len;
  ecewo__req_t *headers;
  ecewo__req_t *query;
  ecewo__req_t *params;
  ecewo__req_ctx_t *ctx;
  uint8_t http_major;
  uint8_t http_minor;
  bool is_head_request;
  void *chain;
} ecewo_request_t;

typedef struct ecewo_response_t {
  ecewo_arena_t *arena;
  uv_tcp_t *ecewo__client_socket;
  uint16_t status;
  char *content_type;
  void *body;
  size_t body_len;
  bool keep_alive;
  ecewo__res_header_t *headers;
  uint16_t header_count;
  uint16_t header_capacity;
  bool replied;
  bool is_head_request;
} ecewo_response_t;

typedef enum {
  // 1xx Informational
  CONTINUE = 100,
  SWITCHING_PROTOCOLS = 101,
  PROCESSING = 102,
  EARLY_HINTS = 103,

  // 2xx Success
  OK = 200,
  CREATED = 201,
  ACCEPTED = 202,
  NON_AUTHORITATIVE_INFORMATION = 203,
  NO_CONTENT = 204,
  RESET_CONTENT = 205,
  PARTIAL_CONTENT = 206,
  MULTI_STATUS = 207,
  ALREADY_REPORTED = 208,
  IM_USED = 226,

  // 3xx Redirection
  MULTIPLE_CHOICES = 300,
  MOVED_PERMANENTLY = 301,
  FOUND = 302,
  SEE_OTHER = 303,
  NOT_MODIFIED = 304,
  USE_PROXY = 305,
  TEMPORARY_REDIRECT = 307,
  PERMANENT_REDIRECT = 308,

  // 4xx Client Error
  BAD_REQUEST = 400,
  UNAUTHORIZED = 401,
  PAYMENT_REQUIRED = 402,
  FORBIDDEN = 403,
  NOT_FOUND = 404,
  METHOD_NOT_ALLOWED = 405,
  NOT_ACCEPTABLE = 406,
  PROXY_AUTHENTICATION_REQUIRED = 407,
  REQUEST_TIMEOUT = 408,
  CONFLICT = 409,
  GONE = 410,
  LENGTH_REQUIRED = 411,
  PRECONDITION_FAILED = 412,
  PAYLOAD_TOO_LARGE = 413,
  URI_TOO_LONG = 414,
  UNSUPPORTED_MEDIA_TYPE = 415,
  RANGE_NOT_SATISFIABLE = 416,
  EXPECTATION_FAILED = 417,
  IM_A_TEAPOT = 418,
  MISDIRECTED_REQUEST = 421,
  UNPROCESSABLE_ENTITY = 422,
  LOCKED = 423,
  FAILED_DEPENDENCY = 424,
  TOO_EARLY = 425,
  UPGRADE_REQUIRED = 426,
  PRECONDITION_REQUIRED = 428,
  TOO_MANY_REQUESTS = 429,
  REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
  UNAVAILABLE_FOR_LEGAL_REASONS = 451,

  // 5xx Server Error
  INTERNAL_SERVER_ERROR = 500,
  NOT_IMPLEMENTED = 501,
  BAD_GATEWAY = 502,
  SERVICE_UNAVAILABLE = 503,
  GATEWAY_TIMEOUT = 504,
  HTTP_VERSION_NOT_SUPPORTED = 505,
  VARIANT_ALSO_NEGOTIATES = 506,
  INSUFFICIENT_STORAGE = 507,
  LOOP_DETECTED = 508,
  NOT_EXTENDED = 510,
  NETWORK_AUTHENTICATION_REQUIRED = 511
} ecewo_status_t;

typedef void (*ecewo_next_t)(ecewo_request_t *, ecewo_response_t *);
typedef void (*ecewo__handler_t)(ecewo_request_t *req, ecewo_response_t *res);
typedef void (*ecewo__middleware_t)(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next);

typedef void (*shutdown_callback_t)(void);
typedef void (*timer_callback_t)(void *user_data);

// APP FUNCTIONS
ecewo_app_t *ecewo_create(void);
int ecewo_bind(ecewo_app_t *app, uint16_t port);
void ecewo_run(ecewo_app_t *app);
void ecewo_listen(ecewo_app_t *app, uint16_t port);
void ecewo_shutdown(ecewo_app_t *app);
void ecewo_atexit(ecewo_app_t *app, shutdown_callback_t callback);

// TIMER FUNCTIONS
ecewo_timer_t *ecewo_timeout(timer_callback_t callback, uint64_t delay_ms, void *user_data);
ecewo_timer_t *ecewo_interval(timer_callback_t callback, uint64_t interval_ms, void *user_data);
void ecewo_clear_timer(ecewo_timer_t *timer);

// Enable request timeout for the current request
// Returns 0 on success, -1 on error
// Call this in your route handler or middleware to set a timeout
// Call it multiple times to reset or renew
int ecewo_timeout_request(ecewo_response_t *res, uint64_t timeout_ms);

// REQUEST FUNCTIONS
const char *ecewo_param(const ecewo_request_t *req, const char *key);
const char *ecewo_query(const ecewo_request_t *req, const char *key);
const char *ecewo_header_get(const ecewo_request_t *req, const char *key);

// ecewo_header_set DOES NOT check for duplicates!
// User is responsible for avoiding duplicate headers.
// Multiple calls with same name will add multiple headers.
void ecewo_header_set(ecewo_response_t *res, const char *name, const char *value);

// RESPONSE FUNCTIONS
void ecewo_send(ecewo_response_t *res, int status, const void *body, size_t body_len);
void ecewo_redirect(ecewo_response_t *res, int status, const char *url);

static inline void ecewo_send_text(ecewo_response_t *res, int status, const char *body) {
  ecewo_header_set(res, "Content-Type", "text/plain");
  ecewo_send(res, status, body, strlen(body));
}

static inline void ecewo_send_html(ecewo_response_t *res, int status, const char *body) {
  ecewo_header_set(res, "Content-Type", "text/html");
  ecewo_send(res, status, body, strlen(body));
}

static inline void ecewo_send_json(ecewo_response_t *res, int status, const char *body) {
  ecewo_header_set(res, "Content-Type", "application/json");
  ecewo_send(res, status, body, strlen(body));
}

// MIDDLEWARE FUNCTIONS

// Internal function, do not use it
void ecewo__register_use(ecewo_app_t *app, const char *path, ecewo__middleware_t middleware_handler);

// Internal macro, do not use it
#define ECEWO__GLOBAL_MIDDLEWARE_BUILD(_1, _2, NAME, ...) NAME
// Internal macro, do not use it
#define ECEWO__GLOBAL_MIDDLEWARE(app, fn) ecewo__register_use(app, NULL, fn)
// Internal macro, do not use it
#define ECEWO__GLOBAL_MIDDLEWARE_PATH(app, path, fn) ecewo__register_use(app, path, fn)

// ECEWO_USE(app, fn) => global middleware, runs for every request
// ECEWO_USE(app, "/path", fn) => prefix middleware, runs only when path starts with "/path"
#define ECEWO_USE(app, ...) ECEWO__GLOBAL_MIDDLEWARE_BUILD(__VA_ARGS__, ECEWO__GLOBAL_MIDDLEWARE_PATH, ECEWO__GLOBAL_MIDDLEWARE)(app, __VA_ARGS__)

#define ECEWO__MIDDLEWARE(...) \
  (sizeof((void *[]) { __VA_ARGS__ }) / sizeof(void *) - 1)

// ROUTE REGISTRATION
typedef enum {
  HTTP_METHOD_DELETE = 0,
  HTTP_METHOD_GET = 1,
  HTTP_METHOD_HEAD = 2,
  HTTP_METHOD_POST = 3,
  HTTP_METHOD_PUT = 4,
  HTTP_METHOD_OPTIONS = 6,
  HTTP_METHOD_PATCH = 28
} ecewo_method_t;

// Internal functions, do not use them directly
void ecewo__register_get(ecewo_app_t *app, const char *path, int mw_count, ...);
void ecewo__register_post(ecewo_app_t *app, const char *path, int mw_count, ...);
void ecewo__register_put(ecewo_app_t *app, const char *path, int mw_count, ...);
void ecewo__register_patch(ecewo_app_t *app, const char *path, int mw_count, ...);
void ecewo__register_delete(ecewo_app_t *app, const char *path, int mw_count, ...);
void ecewo__register_head(ecewo_app_t *app, const char *path, int mw_count, ...);
void ecewo__register_options(ecewo_app_t *app, const char *path, int mw_count, ...);

#define ECEWO_GET(app, path, ...) \
  ecewo__register_get(app, path, ECEWO__MIDDLEWARE(__VA_ARGS__), __VA_ARGS__)

#define ECEWO_POST(app, path, ...) \
  ecewo__register_post(app, path, ECEWO__MIDDLEWARE(__VA_ARGS__), __VA_ARGS__)

#define ECEWO_PUT(app, path, ...) \
  ecewo__register_put(app, path, ECEWO__MIDDLEWARE(__VA_ARGS__), __VA_ARGS__)

#define ECEWO_PATCH(app, path, ...) \
  ecewo__register_patch(app, path, ECEWO__MIDDLEWARE(__VA_ARGS__), __VA_ARGS__)

#define ECEWO_DELETE(app, path, ...) \
  ecewo__register_delete(app, path, ECEWO__MIDDLEWARE(__VA_ARGS__), __VA_ARGS__)

#define ECEWO_HEAD(app, path, ...) \
  ecewo__register_head(app, path, ECEWO__MIDDLEWARE(__VA_ARGS__), __VA_ARGS__)

#define ECEWO_OPTIONS(app, path, ...) \
  ecewo__register_options(app, path, ECEWO__MIDDLEWARE(__VA_ARGS__), __VA_ARGS__)

void ecewo_context_set(ecewo_request_t *req, const char *key, void *data);
void *ecewo_context_get(ecewo_request_t *req, const char *key);

// TASK SPAWN
typedef void (*spawn_handler_t)(void *context);
typedef void (*spawn_done_t)(ecewo_response_t *res, void *context);

// For general async work (no request/response)
int ecewo_spawn(void *context, spawn_handler_t work_fn, spawn_handler_t done_fn);

// For async request handling
int ecewo_spawn_http(ecewo_response_t *res, void *context,
                     spawn_handler_t work_fn,
                     spawn_done_t done_fn);

// Called for each chunk of body data.
typedef void (*BodyDataCb)(ecewo_request_t *req, const uint8_t *data, size_t len);

// Called when the full body has been received.
typedef void (*BodyEndCb)(ecewo_request_t *req, ecewo_response_t *res);

// Place this middleware to enable the body streaming
// The handler will be called before the body arrives; use ecewo_body_on_data()
// and ecewo_body_on_end() to receive it
// Without this middleware the body is fully buffered and available via
// req->body / req->body_len when the handler runs.
void ecewo_body_stream(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next);

// Register a callback for body chunks.
// In streaming mode: called as chunks arrive from the network.
// In buffered mode:  called once synchronously with the full body.
void ecewo_body_on_data(ecewo_request_t *req, BodyDataCb callback);

// Register a callback for end-of-body.
// In streaming mode: called after the last chunk.
// In buffered mode:  called immediately if body_on_data already ran.
void ecewo_body_on_end(ecewo_request_t *req, ecewo_response_t *res, BodyEndCb callback);

// Set the maximum body size in bytes (default: 10MB).
// Returns the previous limit.
size_t ecewo_body_limit(ecewo_request_t *req, size_t max_bytes);

// DEVELOPMENT FUNCTIONS FOR PLUGINS
bool ecewo_client_is_valid(void *ecewo__client_socket_data);
void ecewo_client_ref(ecewo__client_t *client);
void ecewo_client_unref(ecewo__client_t *client);
void ecewo_increment_async_work(void);
void ecewo_decrement_async_work(void);
uv_loop_t *ecewo_get_loop(void);

typedef struct TakeoverConfig {
  void *alloc_cb;
  void *read_cb;
  void *close_cb;
  void *user_data;
} TakeoverConfig;

int ecewo_connection_takeover(ecewo_response_t *res, const TakeoverConfig *config);
uv_tcp_t *ecewo_get_client_handle(ecewo_response_t *res);

// DEBUG FUNCTIONS
bool ecewo_is_running(ecewo_app_t *app);
int ecewo_active_connections(ecewo_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
