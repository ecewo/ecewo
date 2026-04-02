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
#include "http.h"
#include "middleware.h"
#include "route-table.h"
#include "uv.h"
#include "llhttp.h"
#include <stdatomic.h>

#ifndef READ_BUFFER_SIZE
#define READ_BUFFER_SIZE 16384
#endif

struct server_s {
  App *app;
  bool initialized;
  bool running;
  bool shutdown_requested;
  bool server_closed;
  int active_connections;
  atomic_uint_fast16_t async_work_count;
  uv_loop_t *loop;
  uv_tcp_t *tcp_server;
  uv_signal_t sigint_handle;
  uv_signal_t sigterm_handle;
  uv_async_t shutdown_async;
  uv_async_t async_work_handle; // unreffed while idle, reffed while async_work_count > 0
  shutdown_callback_t shutdown_callback;
  client_t *client_list_head;
  uv_timer_t *cleanup_timer;
  uv_timer_t *force_close_timer;
  route_table_t *route_table;
  GlobalMiddlewareEntry *global_middleware;
  uint16_t global_middleware_count;
  uint16_t global_middleware_capacity;
};

struct client_s {
  uv_tcp_t handle;
  uv_buf_t read_buf;
  char buffer[READ_BUFFER_SIZE];
  bool closing;
  bool draining; // True while draining receive buffer before closing
  uint64_t last_activity;
  bool keep_alive_enabled;
  struct client_s *next;

  Arena *connection_arena; // Lives for the duration of the connection

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

  RequestHandler pending_handler;
  void *pending_mw;
  bool handler_pending;
  Req *pending_req;
  Res *pending_res;

  // Saved req/res for streaming requests whose body spans multiple TCP reads.
  // Set on the first TCP read (headers complete, body not yet fully arrived)
  // when body_stream middleware is detected; consumed on a later TCP read
  // (body finally complete) to call body_stream_complete on the original req
  // instead of dispatching a fresh one.
  Req *stream_req;
  Res *stream_res;

  // Pointer back to the server that owns this client
  server_t *srv;
};

void server_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
void server_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);

#endif
