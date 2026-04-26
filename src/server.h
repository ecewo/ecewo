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

#ifndef ECEWO_SERVER_H
#define ECEWO_SERVER_H

#include "ecewo.h"
#include "request.h"
#include "http.h"
#include "middleware.h"
#include "route-table.h"
#include "uv.h"
#include "llhttp.h"
#include <stdatomic.h>

/* Full definitions of the three types that are opaque in the public header.
 * Only internal source files (which include this header) may access fields
 * directly; external code must use the accessor functions. */

struct ecewo_app_s {
  struct ecewo__server_s *server;
  ecewo_arena_t *arena;
  int max_connections;
  int listen_backlog;
  uint64_t idle_timeout_ms;
  uint64_t request_timeout_ms;
  uint64_t cleanup_interval_ms;
  uint64_t shutdown_timeout_ms;
};

struct ecewo_request_s {
  ecewo_app_t *app;
  ecewo_arena_t *arena;
  void *ecewo__client_socket;
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
};

struct ecewo_response_s {
  ecewo_arena_t *arena;
  void *ecewo__client_socket;
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
};

#ifndef READ_BUFFER_SIZE
#define READ_BUFFER_SIZE 16384
#endif

typedef struct ecewo__runtime_s ecewo__runtime_t;

/* Process-level runtime singleton. Owns the shared event loop, signal handlers,
 * the global async-work counter, and the registry of apps. Lazily initialized
 * on the first ecewo_create() and torn down after ecewo_run() returns and the
 * last app has been shut down. */
struct ecewo__runtime_s {
  uv_loop_t *loop;
  bool initialized;
  bool running;
  bool shutdown_requested;
  bool runtime_handles_closed; // sigint/sigterm/shutdown_async/async_work_handle closed
  atomic_uint_fast32_t async_work_count;
  uv_async_t async_work_handle; // unreffed while idle, reffed while async_work_count > 0
  uv_signal_t sigint_handle;
  uv_signal_t sigterm_handle;
  bool signals_installed;
  uv_async_t shutdown_async;
  ecewo_app_t **apps; // all apps ever created; never shrinks until runtime cleanup
  size_t app_count;
  size_t app_capacity;
  size_t live_app_count; // number of apps that have not yet been shut down
};

struct ecewo__server_s {
  ecewo_app_t *app;
  ecewo__runtime_t *runtime;
  bool initialized;
  bool running;
  bool shutdown_requested;
  bool server_closed;
  bool registered; // currently in runtime->apps[]
  int active_connections;
  uv_tcp_t *tcp_server;
  void (*atexit_cb)(void *user_data);
  void *atexit_user_data;
  ecewo_client_t *client_list_head;
  uv_timer_t *cleanup_timer;
  uv_timer_t *force_close_timer;
  route_table_t *route_table;
  GlobalMiddlewareEntry *global_middleware;
  uint16_t global_middleware_count;
  uint16_t global_middleware_capacity;
};

/* Returns the process-level runtime singleton. Used by callers that need the
 * shared event loop or async-work counter (timers, spawn, plugin authors). */
ecewo__runtime_t *ecewo__runtime_get(void);

struct ecewo_client_s {
  uv_tcp_t handle;
  uv_buf_t read_buf;
  char buffer[READ_BUFFER_SIZE];
  bool closing;
  bool draining; // True while draining receive buffer before closing
  uint64_t last_activity;
  bool keep_alive_enabled;
  struct ecewo_client_s *next;

  ecewo_arena_t *connection_arena; // Lives for the duration of the connection

  // Connection-scoped parser and context
  llhttp_t persistent_parser;
  llhttp_settings_t persistent_settings;
  http_context_t persistent_context; // Struct embedded in client; its buffers live in connection_arena
  bool parser_initialized;
  bool request_in_progress; // True while parsing a multi-packet request

  bool taken_over;
  void *takeover_user_data;

  uv_timer_t *request_timeout_timer;
  atomic_int refcount;
  bool valid;

  ecewo_handler_t pending_handler;
  void *pending_mw;
  bool handler_pending;
  ecewo_request_t *pending_req;
  ecewo_response_t *pending_res;

  // Saved req/res for streaming requests whose body spans multiple TCP reads.
  // Set on the first TCP read (headers complete, body not yet fully arrived)
  // when body_stream middleware is detected; consumed on a later TCP read
  // (body finally complete) to call body_stream_complete on the original req
  // instead of dispatching a fresh one.
  ecewo_request_t *stream_req;
  ecewo_response_t *stream_res;

  // Pointer back to the server that owns this client
  ecewo__server_t *srv;
};

void server_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
void server_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);

#endif
