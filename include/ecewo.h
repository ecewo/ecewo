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
#include "ecewo-export.h"

/** Opaque timer handle returned by `ecewo_timeout()` and `ecewo_interval()`. Pass to `ecewo_clear_timer()` to cancel. */
typedef struct ecewo_timer_s ecewo_timer_t;

/** Opaque client handle. For plugin authors only; use `ecewo_client_ref/unref()`. */
typedef struct ecewo_client_s ecewo_client_t;

/**
 * Opaque application instance. Create with ecewo_create(), configure via
 * `ecewo_set_*()` functions, then call `ecewo_listen()` or `ecewo_bind()` + `ecewo_run()`.
 * All configuration must be set before `ecewo_listen()` / `ecewo_run()`.
 */
typedef struct ecewo_app_s ecewo_app_t;

/**
 * Opaque route builder. Create with ecewo_route_new(), optionally add middleware
 * with ecewo_route_middleware(), then finalize with ecewo_route_handler().
 * This is the primary route registration API for FFI consumers.
 */
typedef struct ecewo_route_s ecewo_route_t;

/**
 * Opaque incoming HTTP request. Passed to every handler and middleware.
 * Access fields via the `ecewo_req_*()` accessor functions.
 */
typedef struct ecewo_request_s ecewo_request_t;

/**
 * Opaque HTTP response. Passed to every handler and middleware.
 * Call one of the `ecewo_send*()` functions to finalize and transmit the response.
 * After `ecewo_send()` returns the handle must not be accessed again.
 */
typedef struct ecewo_response_s ecewo_response_t;

/** Standard HTTP status codes. Pass to `ecewo_send()` and its variants. */
typedef enum {
  // 1xx Informational
  ECEWO_CONTINUE = 100,
  ECEWO_SWITCHING_PROTOCOLS = 101,
  ECEWO_PROCESSING = 102,
  ECEWO_EARLY_HINTS = 103,

  // 2xx Success
  ECEWO_OK = 200,
  ECEWO_CREATED = 201,
  ECEWO_ACCEPTED = 202,
  ECEWO_NON_AUTHORITATIVE_INFORMATION = 203,
  ECEWO_NO_CONTENT = 204,
  ECEWO_RESET_CONTENT = 205,
  ECEWO_PARTIAL_CONTENT = 206,
  ECEWO_MULTI_STATUS = 207,
  ECEWO_ALREADY_REPORTED = 208,
  ECEWO_IM_USED = 226,

  // 3xx Redirection
  ECEWO_MULTIPLE_CHOICES = 300,
  ECEWO_MOVED_PERMANENTLY = 301,
  ECEWO_FOUND = 302,
  ECEWO_SEE_OTHER = 303,
  ECEWO_NOT_MODIFIED = 304,
  ECEWO_USE_PROXY = 305,
  ECEWO_TEMPORARY_REDIRECT = 307,
  ECEWO_PERMANENT_REDIRECT = 308,

  // 4xx Client Error
  ECEWO_BAD_REQUEST = 400,
  ECEWO_UNAUTHORIZED = 401,
  ECEWO_PAYMENT_REQUIRED = 402,
  ECEWO_FORBIDDEN = 403,
  ECEWO_NOT_FOUND = 404,
  ECEWO_METHOD_NOT_ALLOWED = 405,
  ECEWO_NOT_ACCEPTABLE = 406,
  ECEWO_PROXY_AUTHENTICATION_REQUIRED = 407,
  ECEWO_REQUEST_TIMEOUT = 408,
  ECEWO_CONFLICT = 409,
  ECEWO_GONE = 410,
  ECEWO_LENGTH_REQUIRED = 411,
  ECEWO_PRECONDITION_FAILED = 412,
  ECEWO_PAYLOAD_TOO_LARGE = 413,
  ECEWO_URI_TOO_LONG = 414,
  ECEWO_UNSUPPORTED_MEDIA_TYPE = 415,
  ECEWO_RANGE_NOT_SATISFIABLE = 416,
  ECEWO_EXPECTATION_FAILED = 417,
  ECEWO_IM_A_TEAPOT = 418,
  ECEWO_MISDIRECTED_REQUEST = 421,
  ECEWO_UNPROCESSABLE_ENTITY = 422,
  ECEWO_LOCKED = 423,
  ECEWO_FAILED_DEPENDENCY = 424,
  ECEWO_TOO_EARLY = 425,
  ECEWO_UPGRADE_REQUIRED = 426,
  ECEWO_PRECONDITION_REQUIRED = 428,
  ECEWO_TOO_MANY_REQUESTS = 429,
  ECEWO_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
  ECEWO_UNAVAILABLE_FOR_LEGAL_REASONS = 451,

  // 5xx Server Error
  ECEWO_INTERNAL_SERVER_ERROR = 500,
  ECEWO_NOT_IMPLEMENTED = 501,
  ECEWO_BAD_GATEWAY = 502,
  ECEWO_SERVICE_UNAVAILABLE = 503,
  ECEWO_GATEWAY_TIMEOUT = 504,
  ECEWO_HTTP_VERSION_NOT_SUPPORTED = 505,
  ECEWO_VARIANT_ALSO_NEGOTIATES = 506,
  ECEWO_INSUFFICIENT_STORAGE = 507,
  ECEWO_LOOP_DETECTED = 508,
  ECEWO_NOT_EXTENDED = 510,
  ECEWO_NETWORK_AUTHENTICATION_REQUIRED = 511
} ecewo_status_t;

/** Signature for the `next()` function passed to middleware. Call it to continue the chain. */
typedef void (*ecewo_next_t)(ecewo_request_t *, ecewo_response_t *);

/** Signature for a route handler: receives the request and response, sends exactly one reply. */
typedef void (*ecewo_handler_t)(ecewo_request_t *req, ecewo_response_t *res);

/** Signature for middleware: receives `req`, `res`, and `next()`. Call `next()` to pass control downstream. */
typedef void (*ecewo_middleware_t)(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next);

/** Callback invoked by `ecewo_timeout()` or `ecewo_interval()` when the timer fires. */
typedef void (*ecewo_timer_cb_t)(void *user_data);

// ---------------------------------------------------------------------------
// APP FUNCTIONS
// ---------------------------------------------------------------------------

/** Allocate and initialize a new application instance with default configuration.
 *  Returns `NULL` on allocation failure. Free all resources by letting the process exit
 *  after `ecewo_run()` returns, or call `ecewo_shutdown()` from a handler to stop cleanly. */
ECEWO_EXPORT ecewo_app_t *ecewo_create(void);

/** Bind the server to port without entering the event loop.
 *  Use this when you need to register timers or perform other setup before calling ecewo_run().
 *  Returns 0 on success, -1 on error. */
ECEWO_EXPORT int ecewo_bind(ecewo_app_t *app, uint16_t port);

/** Start the event loop. Blocks until `ecewo_shutdown()` is called or a signal stops the process.
 *  Call this after `ecewo_bind()`, or use `ecewo_listen()` which combines both steps. */
ECEWO_EXPORT void ecewo_run(ecewo_app_t *app);

/** Bind to port and start the event loop in a single call.
 *  Equivalent to `ecewo_bind()` + `ecewo_run()`. Blocks until shutdown. */
ECEWO_EXPORT void ecewo_listen(ecewo_app_t *app, uint16_t port);

/** Initiate a graceful shutdown. Active connections are drained up to shutdown_timeout_ms,
 *  then forcibly closed. Safe to call from inside a request handler.
 *  ecewo_run() / ecewo_listen() will return once shutdown completes. */
ECEWO_EXPORT void ecewo_shutdown(ecewo_app_t *app);

/** Register a callback to be called during shutdown, before the event loop exits.
 *  Useful for releasing resources such as database connections or thread pools.
 *  user_data is passed to callback unchanged; pass NULL if not needed. */
ECEWO_EXPORT void ecewo_atexit(ecewo_app_t *app,
                               void (*callback)(void *user_data),
                               void *user_data);

// ---------------------------------------------------------------------------
// TIMER FUNCTIONS
// ---------------------------------------------------------------------------

/** Schedule callback to be called once after delay_ms milliseconds.
 *  user_data is passed to the callback unchanged. Returns an opaque timer handle,
 *  or NULL on error. The timer is automatically freed after it fires. */
ECEWO_EXPORT ecewo_timer_t *ecewo_timeout(ecewo_timer_cb_t callback, uint64_t delay_ms, void *user_data);

/** Schedule callback to be called repeatedly every interval_ms milliseconds.
 *  user_data is passed to the callback unchanged. Returns an opaque timer handle,
 *  or NULL on error. Call ecewo_clear_timer() to stop it. */
ECEWO_EXPORT ecewo_timer_t *ecewo_interval(ecewo_timer_cb_t callback, uint64_t interval_ms, void *user_data);

/** Cancel and free a timer returned by ecewo_timeout() or ecewo_interval().
 *  After this call the handle is invalid; do not use it again. */
ECEWO_EXPORT void ecewo_clear_timer(ecewo_timer_t *timer);

/** Enable a per-request deadline for the current request.
 *  If the handler does not send a response within timeout_ms, the connection is closed.
 *  Calling again resets the timeout. Returns 0 on success, -1 on error.
 *  Useful inside handlers that perform slow or async work. */
ECEWO_EXPORT int ecewo_timeout_request(ecewo_response_t *res, uint64_t timeout_ms);

// ---------------------------------------------------------------------------
// REQUEST FUNCTIONS
// ---------------------------------------------------------------------------

/** Return the value of a named path parameter, or NULL if not found.
 *  For a route "/users/:id", ecewo_param(req, "id") returns the captured segment. */
ECEWO_EXPORT const char *ecewo_param(const ecewo_request_t *req, const char *key);

/** Return the value of a named query string parameter, or NULL if not found.
 *  For the URL "/search?q=hello", ecewo_query(req, "q") returns "hello". */
ECEWO_EXPORT const char *ecewo_query(const ecewo_request_t *req, const char *key);

/** Return the value of an incoming request header by name (case-insensitive), or NULL if absent. */
ECEWO_EXPORT const char *ecewo_header_get(const ecewo_request_t *req, const char *key);

/** Append a response header. Does NOT check for duplicates - calling this twice with the
 *  same name will produce two headers in the response. Set Content-Type via this function
 *  or use the ecewo_send_text/html/json() helpers which set it automatically. */
ECEWO_EXPORT void ecewo_header_set(ecewo_response_t *res, const char *name, const char *value);

// ---------------------------------------------------------------------------
// RESPONSE FUNCTIONS
// ---------------------------------------------------------------------------

/** Send a response with the given HTTP status code and raw body.
 *  body may be NULL when body_len is 0 (e.g. for 204 No Content).
 *  Set Content-Type with ecewo_header_set() before calling this, or use a typed helper.
 *  After this call, res must not be accessed again. */
ECEWO_EXPORT void ecewo_send(ecewo_response_t *res, int status, const void *body, size_t body_len);

/** Send an HTTP redirect response to url with the given 3xx status code.
 *  Sets the Location header automatically. After this call, res must not be accessed again. */
ECEWO_EXPORT void ecewo_redirect(ecewo_response_t *res, int status, const char *url);

/** Send a plain-text response (Content-Type: text/plain). Convenience wrapper around ecewo_send(). */
ECEWO_EXPORT void ecewo_send_text(ecewo_response_t *res, int status, const char *body);

/** Send an HTML response (Content-Type: text/html). Convenience wrapper around ecewo_send(). */
ECEWO_EXPORT void ecewo_send_html(ecewo_response_t *res, int status, const char *body);

/** Send a JSON response (Content-Type: application/json). Convenience wrapper around ecewo_send(). */
ECEWO_EXPORT void ecewo_send_json(ecewo_response_t *res, int status, const char *body);

// ---------------------------------------------------------------------------
// MEMORY MANAGEMENT
// ---------------------------------------------------------------------------

typedef struct ecewo_arena_s ecewo_arena_t;

ECEWO_EXPORT void *ecewo_alloc(ecewo_arena_t *arena, size_t size_bytes);
ECEWO_EXPORT void *ecewo_realloc(ecewo_arena_t *arena, void *oldptr, size_t oldsz, size_t newsz);
ECEWO_EXPORT char *ecewo_strdup(ecewo_arena_t *arena, const char *cstr);
ECEWO_EXPORT void *ecewo_memdup(ecewo_arena_t *arena, void *data, size_t size);
ECEWO_EXPORT char *ecewo_sprintf(ecewo_arena_t *arena, const char *format, ...);

ECEWO_EXPORT ecewo_arena_t *ecewo_arena_borrow(void);
ECEWO_EXPORT void ecewo_arena_return(ecewo_arena_t *arena);

#ifdef ECEWO_DEBUG
ECEWO_EXPORT void ecewo_arena_pool_stats(void);
#endif

// ---------------------------------------------------------------------------
// REQUEST ACCESSORS
// ---------------------------------------------------------------------------

/** Return the application instance associated with this request. */
ECEWO_EXPORT ecewo_app_t *ecewo_req_app(const ecewo_request_t *req);

/** Return the per-request arena allocator. All allocations live until the response is sent. */
ECEWO_EXPORT ecewo_arena_t *ecewo_req_arena(const ecewo_request_t *req);

/** Return the HTTP method string (e.g. "GET", "POST"). */
ECEWO_EXPORT const char *ecewo_req_method(const ecewo_request_t *req);

/** Return the request path (e.g. "/users/42"). */
ECEWO_EXPORT const char *ecewo_req_path(const ecewo_request_t *req);

/** Return the raw request body bytes, or NULL when body streaming is active. */
ECEWO_EXPORT const uint8_t *ecewo_req_body(const ecewo_request_t *req);

/** Return the number of bytes in the request body. */
ECEWO_EXPORT size_t ecewo_req_body_len(const ecewo_request_t *req);

/** Return the HTTP major version (1 for HTTP/1.x). */
ECEWO_EXPORT uint8_t ecewo_req_http_major(const ecewo_request_t *req);

/** Return the HTTP minor version (0 or 1). */
ECEWO_EXPORT uint8_t ecewo_req_http_minor(const ecewo_request_t *req);

/** Return true when the request method is HEAD. */
ECEWO_EXPORT bool ecewo_req_is_head(const ecewo_request_t *req);

// ---------------------------------------------------------------------------
// RESPONSE ACCESSORS
// ---------------------------------------------------------------------------

/** Return the per-request arena allocator shared with the request. */
ECEWO_EXPORT ecewo_arena_t *ecewo_res_arena(const ecewo_response_t *res);

// ---------------------------------------------------------------------------
// APP CONFIGURATION
// ---------------------------------------------------------------------------

/** Set the maximum number of simultaneous connections (default: 10000). */
ECEWO_EXPORT void ecewo_set_max_connections(ecewo_app_t *app, int val);

/** Set the TCP listen backlog (default: 511). */
ECEWO_EXPORT void ecewo_set_listen_backlog(ecewo_app_t *app, int val);

/** Set the idle connection timeout in milliseconds; 0 disables (default: 60000). */
ECEWO_EXPORT void ecewo_set_idle_timeout(ecewo_app_t *app, uint64_t ms);

/** Set the per-request timeout in milliseconds; 0 disables (default: 0). */
ECEWO_EXPORT void ecewo_set_request_timeout(ecewo_app_t *app, uint64_t ms);

/** Set how often the cleanup timer runs in milliseconds (default: 30000). */
ECEWO_EXPORT void ecewo_set_cleanup_interval(ecewo_app_t *app, uint64_t ms);

/** Set the graceful shutdown drain timeout in milliseconds (default: 15000). */
ECEWO_EXPORT void ecewo_set_shutdown_timeout(ecewo_app_t *app, uint64_t ms);

// ---------------------------------------------------------------------------
// MIDDLEWARE REGISTRATION
// ---------------------------------------------------------------------------

/**
 * Register a global middleware function.
 *
 * When `path` is `NULL`, `fn` runs for every incoming request regardless of path.
 * When `path` is non-`NULL`, `fn` runs only when the request path starts with that prefix.
 *
 * Middleware is executed in registration order before the route handler.
 * `fn` must call `next(req, res)` to continue the chain, or send a response
 * itself to short-circuit further processing.
 */
ECEWO_EXPORT void ecewo_use(ecewo_app_t *app, const char *path, ecewo_middleware_t fn);

// ---------------------------------------------------------------------------
// ROUTE REGISTRATION
// ---------------------------------------------------------------------------

/** HTTP methods passed to ecewo_route_new(). */
typedef enum {
  ECEWO_METHOD_DELETE = 0,
  ECEWO_METHOD_GET,
  ECEWO_METHOD_HEAD,
  ECEWO_METHOD_POST,
  ECEWO_METHOD_PUT,
  ECEWO_METHOD_OPTIONS,
  ECEWO_METHOD_PATCH
} ecewo_method_t;

// ---------------------------------------------------------------------------
// ROUTE BUILDER API
// ---------------------------------------------------------------------------

/** Start building a route for the given method and path.
 *  Returns an opaque builder handle, or NULL on error.
 *  The builder is arena-allocated and does not need to be freed. */
ECEWO_EXPORT ecewo_route_t *ecewo_route_new(ecewo_app_t *app, ecewo_method_t method, const char *path);

/** Add a middleware function to the route chain.
 *  Middleware is executed in the order it is added, before the final handler.
 *  Must be called before ecewo_route_handler(). */
ECEWO_EXPORT void ecewo_route_middleware(ecewo_route_t *route, ecewo_middleware_t fn);

/** Finalize the route with its handler and register it with the router.
 *  After this call the builder must not be used again. */
ECEWO_EXPORT void ecewo_route_handler(ecewo_route_t *route, ecewo_handler_t handler);

// Internal helper used by the macros below. Do not call directly.
// fns = [middleware0, ..., middlewareN, handler], count = total elements.
ECEWO_EXPORT void ecewo_route_register(ecewo_app_t *app, ecewo_method_t method, const char *path, void **fns, int count);

/**
 * Register a route handler for the given HTTP method and path.
 *
 * path supports named parameters with a colon prefix, e.g. "/users/:id".
 * One or more optional middleware functions may precede the final handler:
 *
 *   ECEWO_GET(app, "/users/:id", auth_middleware, get_user_handler);
 *   ECEWO_POST(app, "/items", create_item_handler);
 *
 * Middleware functions have signature: void mw(req, res, next)
 * The handler function has signature:  void h(req, res)
 *
 * Routes are matched in registration order; the first match wins.
 */
#define ECEWO_GET(app, path, ...)                                                         \
  do {                                                                                    \
    void *fns[] = { __VA_ARGS__ };                                                        \
    ecewo_route_register(app, ECEWO_METHOD_GET, path, fns, sizeof(fns) / sizeof(void *)); \
  } while (0)

/** Register a POST route. See ECEWO_GET for full documentation. */
#define ECEWO_POST(app, path, ...)                                                         \
  do {                                                                                     \
    void *fns[] = { __VA_ARGS__ };                                                         \
    ecewo_route_register(app, ECEWO_METHOD_POST, path, fns, sizeof(fns) / sizeof(void *)); \
  } while (0)

/** Register a PUT route. See ECEWO_GET for full documentation. */
#define ECEWO_PUT(app, path, ...)                                                         \
  do {                                                                                    \
    void *fns[] = { __VA_ARGS__ };                                                        \
    ecewo_route_register(app, ECEWO_METHOD_PUT, path, fns, sizeof(fns) / sizeof(void *)); \
  } while (0)

/** Register a PATCH route. See ECEWO_GET for full documentation. */
#define ECEWO_PATCH(app, path, ...)                                                         \
  do {                                                                                      \
    void *fns[] = { __VA_ARGS__ };                                                          \
    ecewo_route_register(app, ECEWO_METHOD_PATCH, path, fns, sizeof(fns) / sizeof(void *)); \
  } while (0)

/** Register a DELETE route. See ECEWO_GET for full documentation. */
#define ECEWO_DELETE(app, path, ...)                                                         \
  do {                                                                                       \
    void *fns[] = { __VA_ARGS__ };                                                           \
    ecewo_route_register(app, ECEWO_METHOD_DELETE, path, fns, sizeof(fns) / sizeof(void *)); \
  } while (0)

/** Register a HEAD route. ecewo automatically suppresses the body in the response. See ECEWO_GET. */
#define ECEWO_HEAD(app, path, ...)                                                         \
  do {                                                                                     \
    void *fns[] = { __VA_ARGS__ };                                                         \
    ecewo_route_register(app, ECEWO_METHOD_HEAD, path, fns, sizeof(fns) / sizeof(void *)); \
  } while (0)

/** Register an OPTIONS route. See ECEWO_GET for full documentation. */
#define ECEWO_OPTIONS(app, path, ...)                                                         \
  do {                                                                                        \
    void *fns[] = { __VA_ARGS__ };                                                            \
    ecewo_route_register(app, ECEWO_METHOD_OPTIONS, path, fns, sizeof(fns) / sizeof(void *)); \
  } while (0)

// ---------------------------------------------------------------------------
// PER-REQUEST CONTEXT
// ---------------------------------------------------------------------------

/** Attach an arbitrary value to the request under key.
 *  Useful for passing data between middleware and handlers (e.g. authenticated user).
 *  key is compared by string; data must remain valid for the request lifetime. */
ECEWO_EXPORT void ecewo_context_set(ecewo_request_t *req, const char *key, void *data);

/** Retrieve a value previously stored with ecewo_context_set(), or NULL if key is absent. */
ECEWO_EXPORT void *ecewo_context_get(ecewo_request_t *req, const char *key);

// ---------------------------------------------------------------------------
// ASYNC TASK SPAWN
// ---------------------------------------------------------------------------

/** Callback type used for background work in ecewo_spawn() and ecewo_spawn_http().
 *  context is the user-supplied pointer passed at spawn time. */
typedef void (*ecewo_spawn_handler_t)(void *context);

/** Callback type for the completion step of ecewo_spawn_http().
 *  Called on the event-loop thread after work_fn finishes; safe to call ecewo_send() here. */
typedef void (*ecewo_spawn_done_t)(ecewo_response_t *res, void *context);

/** Run work_fn(context) on a thread-pool thread, then call done_fn(context) on the event loop.
 *  Use this for background tasks that are not tied to an HTTP request/response.
 *  Returns 0 on success, -1 on error. */
ECEWO_EXPORT int ecewo_spawn(void *context, ecewo_spawn_handler_t work_fn, ecewo_spawn_handler_t done_fn);

/** Run work_fn(context) on a thread-pool thread, then call done_fn(res, context) on the event loop.
 *  Use this to offload blocking work inside a request handler (e.g. database queries, file I/O).
 *  done_fn is the right place to call ecewo_send(). Returns 0 on success, -1 on error. */
ECEWO_EXPORT int ecewo_spawn_http(ecewo_response_t *res, void *context, ecewo_spawn_handler_t work_fn, ecewo_spawn_done_t done_fn);

// ---------------------------------------------------------------------------
// BODY STREAMING
// ---------------------------------------------------------------------------

/** Called for each chunk of body data received from the client. */
typedef void (*ecewo_body_data_cb_t)(ecewo_request_t *req, const uint8_t *data, size_t len);

/** Called once the full request body has been received. */
typedef void (*ecewo_body_end_cb_t)(ecewo_request_t *req, ecewo_response_t *res);

/** Middleware that enables body streaming for the current request.
 *  Place it before your handler when you want to process the body incrementally.
 *  The handler runs before body data arrives; register ecewo_body_data_cb_t and ecewo_body_end_cb_t
 *  via ecewo_body_on_data() and ecewo_body_on_end() to receive it.
 *  Without this middleware, the body is fully buffered and available in req->body
 *  when the handler is called. */
ECEWO_EXPORT void ecewo_body_stream(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next);

/** Register a callback to receive incoming body data.
 *  In streaming mode: called once per network chunk as data arrives.
 *  In buffered mode:  called once synchronously with the complete body. */
ECEWO_EXPORT void ecewo_body_on_data(ecewo_request_t *req, ecewo_body_data_cb_t callback);

/** Register a callback for end-of-body.
 *  In streaming mode: called after the last chunk has been delivered to ecewo_body_data_cb_t.
 *  In buffered mode:  called immediately if ecewo_body_on_data() has already been set. */
ECEWO_EXPORT void ecewo_body_on_end(ecewo_request_t *req, ecewo_response_t *res, ecewo_body_end_cb_t callback);

/** Set the maximum allowed request body size in bytes (default: 10 MB).
 *  Requests that exceed this limit are rejected with 413 Payload Too Large.
 *  Returns the previous limit. Call this before body data starts arriving. */
ECEWO_EXPORT size_t ecewo_body_limit(ecewo_request_t *req, size_t max_bytes);

// ---------------------------------------------------------------------------
// PLUGIN / ADVANCED API
// ---------------------------------------------------------------------------

/** Return the client handle associated with a request. For plugin authors only. */
ECEWO_EXPORT ecewo_client_t *ecewo_req_client(ecewo_request_t *req);

/** Return the client handle associated with a response. For plugin authors only. */
ECEWO_EXPORT ecewo_client_t *ecewo_res_client(ecewo_response_t *res);

/** Return true if the client connection is still open.
 *  Intended for plugin authors who hold a client pointer across async boundaries. */
ECEWO_EXPORT bool ecewo_client_is_valid(ecewo_client_t *client);

/** Increment the reference count of a client, preventing it from being freed.
 *  Must be paired with a matching ecewo_client_unref(). For plugin authors only. */
ECEWO_EXPORT void ecewo_client_ref(ecewo_client_t *client);

/** Decrement the reference count of a client. When it reaches zero the client is freed.
 *  Must be paired with a prior ecewo_client_ref(). For plugin authors only. */
ECEWO_EXPORT void ecewo_client_unref(ecewo_client_t *client);

/** Notify the server that an asynchronous operation has started.
 *  Prevents the event loop from exiting while the operation is pending.
 *  Must be paired with a matching ecewo_decrement_async_work(). For plugin authors only. */
ECEWO_EXPORT void ecewo_increment_async_work(void);

/** Notify the server that an asynchronous operation has completed.
 *  Must be paired with a prior ecewo_increment_async_work(). For plugin authors only. */
ECEWO_EXPORT void ecewo_decrement_async_work(void);

/** Return the libuv event loop used by the server as a void pointer.
 *  Cast to uv_loop_t * (include uv.h) to use with libuv APIs directly.
 *  Use this to integrate additional libuv handles (e.g. custom I/O watchers) with ecewo. */
ECEWO_EXPORT void *ecewo_get_loop(void);

/**
 * Opaque configuration for taking over a raw TCP connection from ecewo.
 * Used with ecewo_connection_takeover() to implement protocols such as WebSocket
 * that need direct control over the socket after the HTTP upgrade handshake.
 *
 * Create with ecewo_takeover_config_new(), populate via the ecewo_takeover_config_set_*()
 * setters, pass to ecewo_connection_takeover(), then free with ecewo_takeover_config_free().
 */
typedef struct ecewo_takeover_config_s ecewo_takeover_config_t;

/** Allocate a new, zeroed takeover config. Returns NULL on allocation failure. */
ECEWO_EXPORT ecewo_takeover_config_t *ecewo_takeover_config_new(void);

/** Free a takeover config created by ecewo_takeover_config_new(). */
ECEWO_EXPORT void ecewo_takeover_config_free(ecewo_takeover_config_t *config);

/** Set the libuv alloc callback (uv_alloc_cb signature). */
ECEWO_EXPORT void ecewo_takeover_config_set_alloc_cb(ecewo_takeover_config_t *config, void *alloc_cb);

/** Set the libuv read callback (uv_read_cb signature). */
ECEWO_EXPORT void ecewo_takeover_config_set_read_cb(ecewo_takeover_config_t *config, void *read_cb);

/** Set the libuv close callback (uv_close_cb signature).
 *  Called when the connection is closed after takeover. */
ECEWO_EXPORT void ecewo_takeover_config_set_close_cb(ecewo_takeover_config_t *config, void *close_cb);

/** Set user_data, passed to alloc_cb and read_cb via uv_handle->data. */
ECEWO_EXPORT void ecewo_takeover_config_set_user_data(ecewo_takeover_config_t *config, void *user_data);

/** Transfer ownership of the underlying TCP socket from ecewo to the caller.
 *  After this call, ecewo no longer reads from or manages the connection.
 *  Returns 0 on success, -1 on error. Typically called after sending an HTTP 101 upgrade reply. */
ECEWO_EXPORT int ecewo_connection_takeover(ecewo_response_t *res, const ecewo_takeover_config_t *config);

/** Return the raw libuv TCP handle for the connection associated with res as a void pointer.
 *  Cast to uv_tcp_t * (include uv.h) to use with libuv APIs directly.
 *  Use with ecewo_connection_takeover() to write directly to the socket. */
ECEWO_EXPORT void *ecewo_get_client_handle(ecewo_response_t *res);

// ---------------------------------------------------------------------------
// DEBUG / DIAGNOSTIC FUNCTIONS
// ---------------------------------------------------------------------------

/** Return true if the event loop is currently running (i.e. between ecewo_run() and shutdown). */
ECEWO_EXPORT bool ecewo_is_running(ecewo_app_t *app);

/** Return the number of currently open client connections. Useful for monitoring and testing. */
ECEWO_EXPORT int ecewo_active_connections(ecewo_app_t *app);

// Dynamic Array Macros for C Users
// Copyright 2022 Alexey Kutepov <reximkut@gmail.com>
// Copyright 2026 Savas Sahin <savashn@proton.me>

#ifndef ECEWO__DA_INIT_CAP
#define ECEWO__DA_INIT_CAP 256
#endif

#ifdef __cplusplus
#define ECEWO__CAST_PTR(ptr) (decltype(ptr))
#else
#define ECEWO__CAST_PTR(...)
#endif

#define ecewo_da_append(a, da, item)                                                       \
  do {                                                                                     \
    if ((da)->count >= (da)->capacity) {                                                   \
      size_t new_capacity = (da)->capacity == 0 ? ECEWO__DA_INIT_CAP : (da)->capacity * 2; \
      (da)->items = ECEWO__CAST_PTR((da)->items) ecewo_realloc(                            \
          (a), (da)->items,                                                                \
          (da)->capacity * sizeof(*(da)->items),                                           \
          new_capacity * sizeof(*(da)->items));                                            \
      (da)->capacity = new_capacity;                                                       \
    }                                                                                      \
                                                                                           \
    (da)->items[(da)->count++] = (item);                                                   \
  } while (0)

#define ecewo_da_append_many(a, da, new_items, new_items_count)                               \
  do {                                                                                        \
    if ((da)->count + (new_items_count) > (da)->capacity) {                                   \
      size_t new_capacity = (da)->capacity;                                                   \
      if (new_capacity == 0)                                                                  \
        new_capacity = ECEWO__DA_INIT_CAP;                                                    \
      while ((da)->count + (new_items_count) > new_capacity)                                  \
        new_capacity *= 2;                                                                    \
      (da)->items = ECEWO__CAST_PTR((da)->items) ecewo_realloc(                               \
          (a), (da)->items,                                                                   \
          (da)->capacity * sizeof(*(da)->items),                                              \
          new_capacity * sizeof(*(da)->items));                                               \
      (da)->capacity = new_capacity;                                                          \
    }                                                                                         \
    memcpy((da)->items + (da)->count, (new_items), (new_items_count) * sizeof(*(da)->items)); \
    (da)->count += (new_items_count);                                                         \
  } while (0)

#define ecewo_sb_append_cstr(a, sb, cstr) \
  do {                                    \
    const char *s = (cstr);               \
    size_t n = strlen(s);                 \
    ecewo_da_append_many(a, sb, s, n);    \
  } while (0)

#define ecewo_sb_append_null(a, sb) ecewo_da_append(a, sb, 0)

#ifdef __cplusplus
}
#endif

#endif
