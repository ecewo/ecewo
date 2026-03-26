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
#include "arena.h"

typedef struct uv_loop_s uv_loop_t;
typedef struct uv_timer_s uv_timer_t;
typedef struct uv_tcp_s uv_tcp_t;
typedef uv_timer_t Timer;

typedef struct client_s client_t;

// Internal struct, do not use it
typedef struct {
  const char *key;
  const char *value;
} request_item_t;

// Internal struct, do not use it
typedef struct {
  request_item_t *items;
  uint16_t count;
  uint16_t capacity;
} request_t;

typedef struct context_t context_t;

typedef struct Req {
  Arena *arena;
  uv_tcp_t *client_socket;
  char *method;
  char *path;
  uint8_t *body;
  size_t body_len;
  request_t headers;
  request_t query;
  request_t params;
  context_t *ctx;
  uint8_t http_major;
  uint8_t http_minor;
  bool is_head_request;
  void *chain;
} Req;

// Internal struct, do not use it
typedef struct {
  const char *name;
  const char *value;
} http_header_t;

typedef struct Res {
  Arena *arena;
  uv_tcp_t *client_socket;
  uint16_t status;
  char *content_type;
  void *body;
  size_t body_len;
  bool keep_alive;
  http_header_t *headers;
  uint16_t header_count;
  uint16_t header_capacity;
  bool replied;
  bool is_head_request;
} Res;

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
} http_status_t;

typedef void (*Next)(Req *, Res *);
typedef void (*RequestHandler)(Req *req, Res *res);
typedef void (*MiddlewareHandler)(Req *req, Res *res, Next next);

typedef void (*shutdown_callback_t)(void);
typedef void (*timer_callback_t)(void *user_data);

// SERVER FUNCTIONS
int server_init(void);
int server_listen(uint16_t port);
void server_run(void);
void server_shutdown(void);
void server_atexit(shutdown_callback_t callback);

// TIMER FUNCTIONS
Timer *set_timeout(timer_callback_t callback, uint64_t delay_ms, void *user_data);
Timer *set_interval(timer_callback_t callback, uint64_t interval_ms, void *user_data);
void clear_timer(Timer *timer);

// Enable request timeout for the current request
// Returns 0 on success, -1 on error
// Call this in your route handler or middleware to set a timeout
// Call it multiple times to reset or renew
int request_timeout(Res *res, uint64_t timeout_ms);

// REQUEST FUNCTIONS
const char *get_param(const Req *req, const char *key);
const char *get_query(const Req *req, const char *key);
const char *get_header(const Req *req, const char *key);

// RESPONSE FUNCTIONS
void reply(Res *res, int status, const void *body, size_t body_len);
void redirect(Res *res, int status, const char *url);

// set_header DOES NOT check for duplicates!
// User is responsible for avoiding duplicate headers.
// Multiple calls with same name will add multiple headers.
void set_header(Res *res, const char *name, const char *value);

static inline void send_text(Res *res, int status, const char *body) {
  set_header(res, "Content-Type", "text/plain");
  reply(res, status, body, strlen(body));
}

static inline void send_html(Res *res, int status, const char *body) {
  set_header(res, "Content-Type", "text/html");
  reply(res, status, body, strlen(body));
}

static inline void send_json(Res *res, int status, const char *body) {
  set_header(res, "Content-Type", "application/json");
  reply(res, status, body, strlen(body));
}

// MIDDLEWARE FUNCTIONS

// Internal function, do not use it
void ecewo_register_use(const char *path, MiddlewareHandler middleware_handler);

// Internal macro, do not use it
#define ECEWO_USE_SELECT(_1, _2, NAME, ...) NAME
// Internal macro, do not use it
#define ECEWO_USE_GLOBAL(fn) ecewo_register_use(NULL, fn)
// Internal macro, do not use it
#define ECEWO_USE_PATH(path, fn) ecewo_register_use(path, fn)

// use(fn) => global middleware, runs for every request
// use("/path", fn) => prefix middleware, runs only when path starts with "/path"
#define use(...) ECEWO_USE_SELECT(__VA_ARGS__, ECEWO_USE_PATH, ECEWO_USE_GLOBAL)(__VA_ARGS__)

void set_context(Req *req, const char *key, void *data);
void *get_context(Req *req, const char *key);

// TASK SPAWN
typedef void (*spawn_handler_t)(void *context);
typedef void (*spawn_done_t)(Res *res, void *context);

// For general async work (no request/response)
int spawn(void *context, spawn_handler_t work_fn, spawn_handler_t done_fn);

// For async request handling
int spawn_http(Res *res,
               void *context,
               spawn_handler_t work_fn,
               spawn_done_t done_fn);

// ROUTE REGISTRATION
typedef enum {
  HTTP_METHOD_DELETE = 0,
  HTTP_METHOD_GET = 1,
  HTTP_METHOD_HEAD = 2,
  HTTP_METHOD_POST = 3,
  HTTP_METHOD_PUT = 4,
  HTTP_METHOD_OPTIONS = 6,
  HTTP_METHOD_PATCH = 28
} http_method_t;

#define MW(...) \
  (sizeof((void *[]) { __VA_ARGS__ }) / sizeof(void *) - 1)

// Internal function, do not use it
void ecewo_register_get(const char *path, int mw_count, ...);
// Internal function, do not use it
void ecewo_register_post(const char *path, int mw_count, ...);
// Internal function, do not use it
void ecewo_register_put(const char *path, int mw_count, ...);
// Internal function, do not use it
void ecewo_register_patch(const char *path, int mw_count, ...);
// Internal function, do not use it
void ecewo_register_del(const char *path, int mw_count, ...);
// Internal function, do not use it
void ecewo_register_headl(const char *path, int mw_count, ...);
// Internal function, do not use it
void ecewo_register_options(const char *path, int mw_count, ...);

#define get(path, ...) \
  ecewo_register_get(path, MW(__VA_ARGS__), __VA_ARGS__)

#define post(path, ...) \
  ecewo_register_post(path, MW(__VA_ARGS__), __VA_ARGS__)

#define put(path, ...) \
  ecewo_register_put(path, MW(__VA_ARGS__), __VA_ARGS__)

#define patch(path, ...) \
  ecewo_register_patch(path, MW(__VA_ARGS__), __VA_ARGS__)

#define del(path, ...) \
  ecewo_register_del(path, MW(__VA_ARGS__), __VA_ARGS__)

#define head(path, ...) \
  ecewo_register_headl(path, MW(__VA_ARGS__), __VA_ARGS__)

#define options(path, ...) \
  ecewo_register_options(path, MW(__VA_ARGS__), __VA_ARGS__)

// Called for each chunk of body data.
typedef void (*BodyDataCb)(Req *req, const uint8_t *data, size_t len);

// Called when the full body has been received.
typedef void (*BodyEndCb)(Req *req, Res *res);

// Place this middleware to enable the body streaming
// The handler will be called before the body arrives; use body_on_data()
// and body_on_end() to receive it
// Without this middleware the body is fully buffered and available via
// req->body / req->body_len when the handler runs.
void body_stream(Req *req, Res *res, Next next);

// Register a callback for body chunks.
// In streaming mode: called as chunks arrive from the network.
// In buffered mode:  called once synchronously with the full body.
void body_on_data(Req *req, BodyDataCb callback);

// Register a callback for end-of-body.
// In streaming mode: called after the last chunk.
// In buffered mode:  called immediately if body_on_data already ran.
void body_on_end(Req *req, Res *res, BodyEndCb callback);

// Set the maximum body size in bytes (default: 10MB).
// Returns the previous limit.
size_t body_limit(Req *req, size_t max_bytes);

// DEVELOPMENT FUNCTIONS FOR PLUGINS
bool client_is_valid(void *client_socket_data);
void client_ref(client_t *client);
void client_unref(client_t *client);
void increment_async_work(void);
void decrement_async_work(void);
uv_loop_t *get_loop(void);

typedef struct TakeoverConfig {
  void *alloc_cb;
  void *read_cb;
  void *close_cb;
  void *user_data;
} TakeoverConfig;

int connection_takeover(Res *res, const TakeoverConfig *config);
uv_tcp_t *get_client_handle(Res *res);

// DEBUG FUNCTIONS
bool server_is_running(void);
int get_active_connections(void);

#ifdef __cplusplus
}
#endif

#endif
