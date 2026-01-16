#ifndef ECEWO_H
#define ECEWO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct uv_loop_s uv_loop_t;
typedef struct uv_timer_s uv_timer_t;
typedef struct uv_tcp_s uv_tcp_t;
typedef uv_timer_t Timer;

typedef struct ArenaRegion ArenaRegion;
typedef struct client_s client_t;

typedef struct Arena {
  ArenaRegion *begin, *end;
} Arena;

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

typedef struct {
  Arena *arena;
  uv_tcp_t *client_socket;
  char *method;
  char *path;
  char *body;
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

typedef struct {
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

// ARENA FUNCTIONS
void *arena_alloc(Arena *a, size_t size_bytes);
void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz);
char *arena_strdup(Arena *a, const char *cstr);
void *arena_memdup(Arena *a, void *data, size_t size);
char *arena_sprintf(Arena *a, const char *format, ...);
void *arena_memcpy(void *dest, const void *src, size_t n);
void arena_free(Arena *a);

// ARENA POOL
Arena *arena_borrow(void);
void arena_return(Arena *arena);
#ifdef ECEWO_DEBUG
void arena_pool_stats(void);
#endif

// MIDDLEWARE FUNCTIONS
void use(MiddlewareHandler middleware_handler);
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

void register_get(const char *path, int mw_count, ...);
void register_post(const char *path, int mw_count, ...);
void register_put(const char *path, int mw_count, ...);
void register_patch(const char *path, int mw_count, ...);
void register_del(const char *path, int mw_count, ...);
void register_head(const char *path, int mw_count, ...);
void register_options(const char *path, int mw_count, ...);

#define get(path, ...) \
  register_get(path, MW(__VA_ARGS__), __VA_ARGS__)

#define post(path, ...) \
  register_post(path, MW(__VA_ARGS__), __VA_ARGS__)

#define put(path, ...) \
  register_put(path, MW(__VA_ARGS__), __VA_ARGS__)

#define patch(path, ...) \
  register_patch(path, MW(__VA_ARGS__), __VA_ARGS__)

#define del(path, ...) \
  register_del(path, MW(__VA_ARGS__), __VA_ARGS__)

#define head(path, ...) \
  register_head(path, MW(__VA_ARGS__), __VA_ARGS__)

#define options(path, ...) \
  register_options(path, MW(__VA_ARGS__), __VA_ARGS__)

// Get buffered body data (NULL if in streaming mode or no body)
const char *body_bytes(const Req *req);

// Get body length in bytes
size_t body_len(const Req *req);

// Data callback - called for each body chunk
// Return true to continue receiving data, false to pause (backpressure)
typedef bool (*BodyDataCb)(Req *req, const char *data, size_t len, void *ctx);

// End callback - called when all body data has been received
typedef void (*BodyEndCb)(Req *req, void *ctx);

// Error callback - called if body processing encounters an error
typedef void (*BodyErrorCb)(Req *req, const char *error, void *ctx);

// Enable streaming mode with data callback
// Must be called BEFORE body data arrives (first line in handler recommended)
// Once called, body_bytes() will return NULL
void body_on_data(Req *req, BodyDataCb callback, void *ctx);
void body_on_end(Req *req, BodyEndCb callback, void *ctx);
void body_on_error(Req *req, BodyErrorCb callback, void *ctx);

// Stops reading from socket until body_resume() is called
void body_pause(Req *req);

void body_resume(Req *req);

// Set maximum body size in bytes (default: 1MB)
// Requests exceeding this size will trigger the error callback
// Returns the previous limit
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
int get_pending_async_work(void);

#ifdef __cplusplus
}
#endif

#endif
