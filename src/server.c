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

#include <stdlib.h>
#include <signal.h>
#include <inttypes.h>
#include "server.h"
#include "route-table.h"
#include "middleware.h"
#include "router.h"
#include "arena-internal.h"
#include "utils.h"
#include "logger.h"

// Global pointer to the current server instance.
// Used by functions called from handlers that have no direct app reference
// (set_timeout, set_interval, get_loop, increment/decrement_async_work).
// TODO: We might need to add the app param to those functions too
static server_t *g_srv = NULL;

typedef enum {
  SERVER_OK = 0,
  SERVER_ALREADY_INITIALIZED = -1,
  SERVER_NOT_INITIALIZED = -2,
  SERVER_ALREADY_RUNNING = -3,
  SERVER_INIT_FAILED = -4,
  SERVER_OUT_OF_MEMORY = -5,
  SERVER_BIND_FAILED = -6,
  SERVER_LISTEN_FAILED = -7,
  SERVER_INVALID_PORT = -8,
} server_error_t;

typedef struct timer_data_s {
  timer_callback_t callback;
  void *user_data;
  bool is_interval;
} timer_data_t;

static void client_free_internal(client_t *client) {
  if (!client)
    return;
  if (client->connection_arena)
    ecewo_arena_return(client->connection_arena);
  free(client);
}

void ecewo_client_ref(client_t *client) {
  if (!client)
    return;
  atomic_fetch_add_explicit(&client->refcount, 1, memory_order_relaxed);
}

void ecewo_client_unref(client_t *client) {
  if (!client)
    return;
  int prev = atomic_fetch_sub_explicit(&client->refcount, 1, memory_order_acq_rel);
  if (prev <= 1)
    client_free_internal(client);
}

static void add_client_to_list(server_t *srv, client_t *client) {
  client->next = srv->client_list_head;
  srv->client_list_head = client;
}

static void remove_client_from_list(server_t *srv, client_t *client) {
  if (!client)
    return;

  if (srv->client_list_head == client) {
    srv->client_list_head = client->next;
    return;
  }

  client_t *current = srv->client_list_head;
  while (current && current->next != client) {
    current = current->next;
  }

  if (current)
    current->next = client->next;
}

static void on_client_closed(uv_handle_t *handle) {
  client_t *client = (client_t *)handle->data;
  if (!client)
    return;

  server_t *srv = client->srv;
  if (srv) {
    remove_client_from_list(srv, client);
    if (srv->active_connections > 0)
      srv->active_connections--;

    if (srv->shutdown_requested && srv->active_connections == 0
        && srv->force_close_timer) {
      uv_timer_stop(srv->force_close_timer);
      uv_close((uv_handle_t *)srv->force_close_timer, (uv_close_cb)free);
      srv->force_close_timer = NULL;
    }
  }

  client->valid = false;

  ecewo_client_unref(client);
}

// Called when the write-side shutdown completes or is cancelled.
// Drain mode keeps the read side alive until the peer closes.
static void on_drain_shutdown(uv_shutdown_t *req, int status) {
  client_t *client = (client_t *)req->data;
  free(req);

  if (uv_is_closing((uv_handle_t *)&client->handle)) {
    ecewo_client_unref(client);
    return;
  }

  if (status < 0) {
    client->draining = false;
    uv_read_stop((uv_stream_t *)&client->handle);
    uv_close((uv_handle_t *)&client->handle, on_client_closed);
  } else {
    uv_read_stop((uv_stream_t *)&client->handle);
    uv_read_start((uv_stream_t *)&client->handle, server_alloc_buffer, server_on_read);
  }

  ecewo_client_unref(client); // release the drain reference
}

static void close_client(client_t *client) {
  if (!client || client->closing)
    return;

  if (client->request_timeout_timer) {
    uv_timer_stop(client->request_timeout_timer);
    uv_close((uv_handle_t *)client->request_timeout_timer, (uv_close_cb)free);
    client->request_timeout_timer = NULL;
  }

  client->closing = true;
  client->valid = false;

  if (!uv_is_closing((uv_handle_t *)&client->handle)) {
    // Shut down the write side and drain the receive buffer before closing.
    // uv_shutdown() waits for any pending writes (e.g. a 413 reply) to
    // finish, then the drain loop in server_on_read
    // discards incoming data until the peer closes its end
    uv_shutdown_t *req = malloc(sizeof(uv_shutdown_t));
    if (req) {
      req->data = client;
      if (uv_shutdown(req, (uv_stream_t *)&client->handle, on_drain_shutdown) == 0) {
        client->draining = true;
        ecewo_client_ref(client); // on_drain_shutdown releases it
        return;
      }
      free(req);
    }

    uv_read_stop((uv_stream_t *)&client->handle);
    uv_close((uv_handle_t *)&client->handle, on_client_closed);
  }
}

static void cleanup_idle_connections(uv_timer_t *handle) {
  server_t *srv = (server_t *)handle->data;
  if (!srv || srv->shutdown_requested)
    return;

  uint64_t idle_timeout = srv->app->idle_timeout_ms;
  uint64_t now = uv_now(srv->loop);
  client_t *current = srv->client_list_head;

  while (current) {
    client_t *next = current->next;

    if (current->taken_over) {
      current = next;
      continue;
    }

    if (current->keep_alive_enabled && !current->closing) {
      uint64_t idle_time = now - current->last_activity;
      if (idle_time > idle_timeout)
        close_client(current);
    }
    current = next;
  }
}

static int start_cleanup_timer(server_t *srv) {
  srv->cleanup_timer = malloc(sizeof(uv_timer_t));
  if (!srv->cleanup_timer)
    return -1;

  if (uv_timer_init(srv->loop, srv->cleanup_timer) != 0) {
    free(srv->cleanup_timer);
    srv->cleanup_timer = NULL;
    return -1;
  }

  srv->cleanup_timer->data = srv;

  uint64_t interval = srv->app->cleanup_interval_ms;
  if (uv_timer_start(srv->cleanup_timer, cleanup_idle_connections, interval, interval) != 0) {
    uv_close((uv_handle_t *)srv->cleanup_timer, (uv_close_cb)free);
    srv->cleanup_timer = NULL;
    return -1;
  }

  return 0;
}

static void stop_cleanup_timer(server_t *srv) {
  if (srv->cleanup_timer) {
    uv_timer_stop(srv->cleanup_timer);
    uv_close((uv_handle_t *)srv->cleanup_timer, (uv_close_cb)free);
    srv->cleanup_timer = NULL;
  }
}

void ecewo_increment_async_work(void) {
  if (!g_srv)
    return;

  uint_fast16_t prev = atomic_fetch_add_explicit(
      &g_srv->async_work_count, 1, memory_order_relaxed);

  if (prev == 0)
    uv_ref((uv_handle_t *)&g_srv->async_work_handle);
}

void ecewo_decrement_async_work(void) {
  if (!g_srv)
    return;

  uint_fast16_t prev = atomic_fetch_sub_explicit(
      &g_srv->async_work_count, 1, memory_order_acq_rel);

  if (prev == 0) {
    LOG_ERROR("Async work counter underflow!");
    atomic_store_explicit(&g_srv->async_work_count, 0, memory_order_release);
    return;
  }

  if (prev == 1)
    uv_unref((uv_handle_t *)&g_srv->async_work_handle);
}

static int client_connection_init(client_t *client) {
  if (!client)
    return -1;

  client->connection_arena = ecewo_arena_borrow();
  if (!client->connection_arena)
    return -1;

  return 0;
}

static void client_parser_init(client_t *client) {
  if (!client || client->parser_initialized)
    return;

  llhttp_settings_init(&client->persistent_settings);

  client->persistent_settings.on_url = on_url_cb;
  client->persistent_settings.on_header_field = on_header_field_cb;
  client->persistent_settings.on_header_value = on_header_value_cb;
  client->persistent_settings.on_method = on_method_cb;
  client->persistent_settings.on_body = on_body_cb;
  client->persistent_settings.on_headers_complete = on_headers_complete_cb;
  client->persistent_settings.on_message_complete = on_message_complete_cb;

  llhttp_init(&client->persistent_parser, HTTP_REQUEST, &client->persistent_settings);

  llhttp_set_lenient_headers(&client->persistent_parser, 0);
  llhttp_set_lenient_chunked_length(&client->persistent_parser, 0);
  llhttp_set_lenient_keep_alive(&client->persistent_parser, 0);

  client->parser_initialized = true;
}

static void client_context_init(client_t *client) {
  if (!client || !client->connection_arena)
    return;

  if (!client->parser_initialized) {
    client_parser_init(client);
  }

  http_context_init(&client->persistent_context,
                    client->connection_arena,
                    &client->persistent_parser,
                    &client->persistent_settings);
}

static void client_context_reset(client_t *client) {
  if (!client || !client->connection_arena)
    return;

  llhttp_reset(&client->persistent_parser);
  http_context_init(&client->persistent_context,
                    client->connection_arena,
                    &client->persistent_parser,
                    &client->persistent_settings);
}

static void close_cb(uv_handle_t *handle) {
  if (handle->data) {
    free(handle->data);
    handle->data = NULL;
  }
}

static void close_walk_cb(uv_handle_t *handle, void *arg) {
  (void)arg;

  if (uv_is_closing(handle))
    return;

  if (handle->type == UV_TIMER) {
    uv_timer_stop((uv_timer_t *)handle);
  } else if (handle->type == UV_SIGNAL) {
    uv_signal_stop((uv_signal_t *)handle);
  }

  if (handle->type == UV_TCP && handle->data != NULL)
    uv_close(handle, on_client_closed);
  else
    uv_close(handle, close_cb);
}

static void on_server_closed(uv_handle_t *handle) {
  server_t *srv = (server_t *)handle->data;
  free(handle);
  if (srv) {
    srv->tcp_server = NULL;
    srv->server_closed = true;
  }
}

static void on_async_work_noop(uv_async_t *handle) {
  (void)handle;
}

static void on_force_close_timeout(uv_timer_t *handle) {
  server_t *srv = (server_t *)handle->data;
  uv_timer_stop(handle);
  uv_close((uv_handle_t *)handle, (uv_close_cb)free);
  if (!srv)
    return;

  srv->force_close_timer = NULL;

  client_t *current = srv->client_list_head;
  while (current) {
    client_t *next = current->next;
    if (!current->closing && !uv_is_closing((uv_handle_t *)&current->handle))
      close_client(current);
    current = next;
  }
}

void ecewo_shutdown(App *app) {
  if (!app || !app->internal)
    return;

  server_t *srv = app->internal;

  if (srv->shutdown_requested)
    return;

  srv->shutdown_requested = true;
  srv->running = false;

  // Stop accepting new connections first.
  if (srv->tcp_server && !uv_is_closing((uv_handle_t *)srv->tcp_server)) {
    srv->tcp_server->data = srv;
    uv_close((uv_handle_t *)srv->tcp_server, on_server_closed);
  }

  // Unref cleanup timer so it no longer keeps the loop alive.
  // stop_cleanup_timer() in server_cleanup() will fully close it.
  if (srv->cleanup_timer)
    uv_unref((uv_handle_t *)srv->cleanup_timer);

  const char *is_worker = getenv("ECEWO_WORKER");
  bool in_cluster = (is_worker && strcmp(is_worker, "1") == 0);

  if (!in_cluster) {
    if (!uv_is_closing((uv_handle_t *)&srv->sigint_handle)) {
      uv_signal_stop(&srv->sigint_handle);
      uv_close((uv_handle_t *)&srv->sigint_handle, NULL);
    }

    if (!uv_is_closing((uv_handle_t *)&srv->sigterm_handle)) {
      uv_signal_stop(&srv->sigterm_handle);
      uv_close((uv_handle_t *)&srv->sigterm_handle, NULL);
    }
  }

  if (!uv_is_closing((uv_handle_t *)&srv->shutdown_async))
    uv_close((uv_handle_t *)&srv->shutdown_async, NULL);

  // Close idle connections immediately; in-progress ones close themselves
  // when their request finishes. The force-close timer is the backstop.
  client_t *current = srv->client_list_head;
  while (current) {
    client_t *next = current->next;
    if (!current->request_in_progress && !current->closing)
      close_client(current);
    current = next;
  }

  // Arm a hard timeout as backstop for connections that take too long.
  // on_client_closed() cancels it early once all connections drain.
  if (srv->active_connections > 0) {
    srv->force_close_timer = malloc(sizeof(uv_timer_t));
    if (srv->force_close_timer) {
      uv_timer_init(srv->loop, srv->force_close_timer);
      srv->force_close_timer->data = srv;
      uv_timer_start(srv->force_close_timer, on_force_close_timeout,
                     srv->app->shutdown_timeout_ms, 0);
    }
  }

  // Return immediately. The outer uv_run() in server_run() exits naturally
  // once all handles (clients, force-close timer, async_work_handle) are
  // closed or unreffed.
}

#ifdef ECEWO_DEBUG
static void inspect_loop(uv_loop_t *loop);
#endif

static void server_cleanup(server_t *srv) {
  if (!srv || !srv->initialized)
    return;

  if (!srv->shutdown_requested)
    ecewo_shutdown(srv->app);

  // uv_run() has returned — all in-progress requests are done.
  // Safe to call the user's shutdown callback while the loop is still valid.
  if (srv->shutdown_callback)
    srv->shutdown_callback();

  // Fully close the cleanup timer (was only unref'd in ecewo_shutdown).
  stop_cleanup_timer(srv);

  // Final walk to close any handles that slipped through.
  uv_walk(srv->loop, close_walk_cb, NULL);
  while (uv_run(srv->loop, UV_RUN_DEFAULT) != 0)
    ;

  // Router cleanup
  if (srv->route_table) {
    route_table_free(srv->route_table);
    srv->route_table = NULL;
  }

  reset_middleware(srv);

  if (srv->app && srv->app->arena) {
    ecewo_arena_return(srv->app->arena);
    srv->app->arena = NULL;
  }

  arena_pool_destroy();
  destroy_date_cache();

#ifdef ECEWO_DEBUG
  inspect_loop(srv->loop);
#endif

  int result = uv_loop_close(srv->loop);
  if (result != 0)
    LOG_ERROR("uv_loop_close failed: %s", uv_strerror(result));

  if (srv->tcp_server && !srv->server_closed)
    free(srv->tcp_server);

  free(srv->loop);
  srv->loop = NULL;
  srv->initialized = false;
}

static void global_server_cleanup(void) {
  if (g_srv)
    server_cleanup(g_srv);
}

static void on_async_shutdown(uv_async_t *handle) {
  server_t *srv = (server_t *)handle->data;
  if (srv)
    ecewo_shutdown(srv->app);
}

static void on_signal(uv_signal_t *handle, int signum) {
  server_t *srv = (server_t *)handle->data;

  if (!srv || srv->shutdown_requested)
    return;

#ifdef ECEWO_DEBUG
  const char *signal_name = (signum == SIGINT) ? "SIGINT" : "SIGTERM";
  LOG_DEBUG("Received %s, shutting down...", signal_name);
#else
  (void)signum;
#endif

  uv_async_send(&srv->shutdown_async);
}

int ecewo_connection_takeover(Res *res, const TakeoverConfig *config) {
  if (!res || !res->client_socket || !config) {
    LOG_ERROR("connection_takeover: Invalid arguments");
    return -1;
  }

  uv_tcp_t *handle = res->client_socket;
  client_t *client = (client_t *)handle->data;

  if (!client) {
    LOG_ERROR("connection_takeover: No client data");
    return -1;
  }

  if (client->taken_over) {
    LOG_ERROR("connection_takeover: Already taken over");
    return -1;
  }

  uv_read_stop((uv_stream_t *)handle);

  client->taken_over = true;
  client->takeover_user_data = config->user_data;

  res->replied = true;

  if (config->read_cb && config->alloc_cb) {
    handle->data = config->user_data;

    int result = uv_read_start((uv_stream_t *)handle,
                               (uv_alloc_cb)config->alloc_cb,
                               (uv_read_cb)config->read_cb);
    if (result != 0) {
      LOG_ERROR("connection_takeover: uv_read_start failed: %s", uv_strerror(result));
      return -1;
    }
  }

  return 0;
}

uv_tcp_t *ecewo_get_client_handle(Res *res) {
  return res ? res->client_socket : NULL;
}

#ifdef ECEWO_DEBUG
static const char *handle_type_name(uv_handle_type t) {
  switch (t) {
  case UV_UNKNOWN_HANDLE:
    return "UNKNOWN";
  case UV_ASYNC:
    return "ASYNC";
  case UV_CHECK:
    return "CHECK";
  case UV_FS_EVENT:
    return "FS_EVENT";
  case UV_FS_POLL:
    return "FS_POLL";
  case UV_HANDLE:
    return "HANDLE";
  case UV_IDLE:
    return "IDLE";
  case UV_NAMED_PIPE:
    return "NAMED_PIPE";
  case UV_POLL:
    return "POLL";
  case UV_PREPARE:
    return "PREPARE";
  case UV_PROCESS:
    return "PROCESS";
  case UV_STREAM:
    return "STREAM";
  case UV_TCP:
    return "TCP";
  case UV_TIMER:
    return "TIMER";
  case UV_TTY:
    return "TTY";
  case UV_UDP:
    return "UDP";
  case UV_SIGNAL:
    return "SIGNAL";
  default:
    return "OTHER";
  }
}

static void inspect_handle_cb(uv_handle_t *handle, void *arg) {
  (void)arg;
  fprintf(stderr,
          "loop-handle: type=%s closing=%d data=%p\n",
          handle_type_name(handle->type),
          uv_is_closing(handle),
          handle->data);
}

static void inspect_loop(uv_loop_t *loop) {
  fprintf(stderr, "Inspecting loop %p\n", (void *)loop);
  uv_walk(loop, inspect_handle_cb, NULL);
}
#endif

static void on_request_timeout(uv_timer_t *handle) {
  client_t *client = (client_t *)handle->data;

  LOG_ERROR("Request timeout - closing connection");

  if (client) {
    if (client->connection_arena)
      arena_reset(client->connection_arena);

    client->request_timeout_timer = NULL;
    close_client(client);
  }

  uv_timer_stop(handle);
  uv_close((uv_handle_t *)handle, (uv_close_cb)free);
}

static void stop_request_timer(client_t *client) {
  if (!client || !client->request_timeout_timer)
    return;

  uv_timer_stop(client->request_timeout_timer);
}

int ecewo_request_timeout(Res *res, uint64_t timeout_ms) {
  if (!res || !res->client_socket || !res->client_socket->data)
    return -1;

  client_t *client = (client_t *)res->client_socket->data;

  if (!client || client->closing || !client->srv)
    return -1;

  if (client->request_timeout_timer) {
    return uv_timer_start(client->request_timeout_timer,
                          on_request_timeout,
                          timeout_ms,
                          0);
  }

  client->request_timeout_timer = malloc(sizeof(uv_timer_t));
  if (!client->request_timeout_timer)
    return -1;

  if (uv_timer_init(client->srv->loop, client->request_timeout_timer) != 0) {
    free(client->request_timeout_timer);
    client->request_timeout_timer = NULL;
    return -1;
  }

  client->request_timeout_timer->data = client;

  if (uv_timer_start(client->request_timeout_timer,
                     on_request_timeout,
                     timeout_ms,
                     0)
      != 0) {
    uv_timer_t *timer = client->request_timeout_timer;
    client->request_timeout_timer = NULL;
    uv_close((uv_handle_t *)timer, (uv_close_cb)free);
    return -1;
  }

  return 0;
}

void server_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  (void)suggested_size;
  client_t *client = (client_t *)handle->data;

  if (!client || (client->closing && !client->draining)
      || (client->srv && client->srv->shutdown_requested)) {
    buf->base = NULL;
    buf->len = 0;
    return;
  }

  *buf = client->read_buf;
}

void server_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  client_t *client = (client_t *)stream->data;

  if (!client)
    return;

  server_t *srv = client->srv;

  if (client->draining) {
    if (nread < 0) {
      uv_read_stop(stream);
      if (!uv_is_closing((uv_handle_t *)&client->handle))
        uv_close((uv_handle_t *)&client->handle, on_client_closed);
    }
    return;
  }

  if (client->closing)
    return;

  if (srv && srv->shutdown_requested) {
    close_client(client);
    return;
  }

  if (nread < 0) {
    close_client(client);
    return;
  }

  if (nread == 0)
    return;

  client->last_activity = srv ? uv_now(srv->loop) : 0;

  if (!client->parser_initialized) {
    client_parser_init(client);
    client_context_init(client);
    client->request_in_progress = false;
  }

  if (!client->request_in_progress) {
    client_context_reset(client);
    client->request_in_progress = true;

    // Start per-request timeout if configured
    if (srv && srv->app && srv->app->request_timeout_ms > 0) {
      if (client->request_timeout_timer) {
        uv_timer_start(client->request_timeout_timer,
                       on_request_timeout,
                       srv->app->request_timeout_ms,
                       0);
      } else {
        client->request_timeout_timer = malloc(sizeof(uv_timer_t));
        if (client->request_timeout_timer) {
          if (uv_timer_init(srv->loop, client->request_timeout_timer) == 0) {
            client->request_timeout_timer->data = client;
            if (uv_timer_start(client->request_timeout_timer,
                               on_request_timeout,
                               srv->app->request_timeout_ms,
                               0)
                != 0) {
              uv_close((uv_handle_t *)client->request_timeout_timer, (uv_close_cb)free);
              client->request_timeout_timer = NULL;
            }
          } else {
            free(client->request_timeout_timer);
            client->request_timeout_timer = NULL;
          }
        }
      }
    }
  }

  if (buf && buf->base) {
    ecewo_client_ref(client);
    int result = router(client, buf->base, (size_t)nread);

    switch (result) {
    case REQUEST_KEEP_ALIVE:
      stop_request_timer(client);
      client->keep_alive_enabled = true;
      break;

    case REQUEST_CLOSE:
      close_client(client);
      break;

    case REQUEST_PENDING:
      break;

    default:
      close_client(client);
      break;
    }

    ecewo_client_unref(client);
  }
}

static void on_connection(uv_stream_t *server, int status) {
  if (status < 0) {
    LOG_ERROR("Connection error");
    return;
  }

  server_t *srv = (server_t *)server->data;
  if (!srv)
    return;

  if (srv->shutdown_requested)
    return;

  if (srv->active_connections >= srv->app->max_connections) {
    LOG_DEBUG("Max connections (%d) reached", srv->app->max_connections);
    return;
  }

  client_t *client = calloc(1, sizeof(client_t));
  if (!client)
    return;

  client->valid = true;
  client->last_activity = uv_now(srv->loop);
  client->keep_alive_enabled = false;
  client->next = NULL;
  client->parser_initialized = false;
  client->request_in_progress = false;
  client->connection_arena = NULL;
  client->srv = srv;

  atomic_init(&client->refcount, 1);

  if (client_connection_init(client) != 0) {
    free(client);
    return;
  }

  if (uv_tcp_init(srv->loop, &client->handle) != 0) {
    if (client->connection_arena)
      ecewo_arena_return(client->connection_arena);
    free(client);
    return;
  }

  client->handle.data = client;
  client->read_buf = uv_buf_init(client->buffer, READ_BUFFER_SIZE);

  if (uv_accept(server, (uv_stream_t *)&client->handle) == 0) {
    uv_tcp_nodelay(&client->handle, 1);

    if (uv_read_start((uv_stream_t *)&client->handle,
                      server_alloc_buffer,
                      server_on_read)
        == 0) {
      add_client_to_list(srv, client);
      srv->active_connections++;
    } else {
      close_client(client);
    }
  } else {
    close_client(client);
  }
}

App *ecewo_create(void) {
  App *app = calloc(1, sizeof(App));
  if (!app)
    return NULL;

  app->max_connections = 10000;
  app->listen_backlog = 511;
  app->idle_timeout_ms = 60000;
  app->request_timeout_ms = 0;
  app->cleanup_interval_ms = 30000;
  app->shutdown_timeout_ms = 15000;

  server_t *srv = calloc(1, sizeof(server_t));
  if (!srv) {
    free(app);
    return NULL;
  }

  srv->app = app;
  app->internal = srv;

  srv->loop = malloc(sizeof(uv_loop_t));
  if (!srv->loop) {
    free(srv);
    free(app);
    return NULL;
  }

  uv_loop_init(srv->loop);

  arena_pool_init();

  if (!arena_pool_is_initialized()) {
    LOG_ERROR("Arena pool initialization failed");
    free(srv->loop);
    free(srv);
    free(app);
    return NULL;
  }

  app->arena = ecewo_arena_borrow();
  if (!app->arena) {
    LOG_ERROR("App arena allocation failed");
    arena_pool_destroy();
    free(srv->loop);
    free(srv);
    free(app);
    return NULL;
  }

  init_date_cache();

  const char *is_worker = getenv("ECEWO_WORKER");
  bool in_cluster = (is_worker && strcmp(is_worker, "1") == 0);

  if (!in_cluster) {
    if (uv_signal_init(srv->loop, &srv->sigint_handle) != 0
        || uv_signal_init(srv->loop, &srv->sigterm_handle) != 0) {
      free(srv->loop);
      free(srv);
      free(app);
      return NULL;
    }

    srv->sigint_handle.data = srv;
    srv->sigterm_handle.data = srv;

    uv_signal_start(&srv->sigint_handle, on_signal, SIGINT);
    uv_signal_start(&srv->sigterm_handle, on_signal, SIGTERM);
  }

  if (uv_async_init(srv->loop, &srv->shutdown_async, on_async_shutdown) != 0) {
    free(srv->loop);
    free(srv);
    free(app);
    return NULL;
  }

  srv->shutdown_async.data = srv;

  if (uv_async_init(srv->loop, &srv->async_work_handle, on_async_work_noop) != 0) {
    free(srv->loop);
    free(srv);
    free(app);
    return NULL;
  }

  // Unreffed by default, only refedd while async work is in flight
  uv_unref((uv_handle_t *)&srv->async_work_handle);

  atomic_init(&srv->async_work_count, 0);

  srv->route_table = route_table_create();
  if (!srv->route_table) {
    LOG_ERROR("Failed to create route table");
    free(srv->loop);
    free(srv);
    free(app);
    return NULL;
  }

  srv->initialized = true;

  g_srv = srv;
  atexit(global_server_cleanup);

  return app;
}

int ecewo_bind(App *app, uint16_t port) {
  if (!app || !app->internal)
    return SERVER_NOT_INITIALIZED;

  server_t *srv = app->internal;

  if (port == 0) {
    LOG_ERROR("Invalid port %" PRIu16 " (must be 1-65535)", port);
    return SERVER_INVALID_PORT;
  }

  if (!srv->initialized)
    return SERVER_NOT_INITIALIZED;

  if (srv->running)
    return SERVER_ALREADY_RUNNING;

  srv->tcp_server = malloc(sizeof(uv_tcp_t));
  if (!srv->tcp_server)
    return SERVER_OUT_OF_MEMORY;

  if (uv_tcp_init(srv->loop, srv->tcp_server) != 0) {
    free(srv->tcp_server);
    srv->tcp_server = NULL;
    return SERVER_INIT_FAILED;
  }

  // Store srv in the tcp server handle so on_connection can retrieve it
  srv->tcp_server->data = srv;

  uv_tcp_simultaneous_accepts(srv->tcp_server, 1);

  struct sockaddr_in addr;
  uv_ip4_addr("0.0.0.0", port, &addr);

  unsigned int flags = 0;

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(ECEWO_TEST_MODE)
  flags = UV_TCP_REUSEPORT;
#endif

  if (uv_tcp_bind(srv->tcp_server, (const struct sockaddr *)&addr, flags) != 0) {
    uv_close((uv_handle_t *)srv->tcp_server, on_server_closed);
    LOG_ERROR("Failed to bind to port %" PRIu16 " (may be in use)", port);
    return SERVER_BIND_FAILED;
  }

  if (uv_listen((uv_stream_t *)srv->tcp_server, srv->app->listen_backlog, on_connection) != 0) {
    uv_close((uv_handle_t *)srv->tcp_server, on_server_closed);
    LOG_ERROR("Failed to listen on port %" PRIu16, port);
    return SERVER_LISTEN_FAILED;
  }

  if (start_cleanup_timer(srv) != 0)
    LOG_DEBUG("Failed to start cleanup timer");

  srv->running = true;

  const char *is_worker = getenv("ECEWO_WORKER");
  if (!is_worker || strcmp(is_worker, "1") != 0)
    printf("Server listening on http://localhost:%" PRIu16 "\n", port);

  return SERVER_OK;
}

void ecewo_run(App *app) {
  if (!app || !app->internal)
    return;

  server_t *srv = app->internal;

  if (!srv->initialized || !srv->running) {
    LOG_ERROR("Server not initialized or not listening");
    return;
  }

  uv_run(srv->loop, UV_RUN_DEFAULT);

  server_cleanup(srv);
}

void ecewo_listen(App *app, uint16_t port) {
  if (ecewo_bind(app, port) != 0)
    return;

  ecewo_run(app);
}

void ecewo_atexit(App *app, shutdown_callback_t callback) {
  if (app && app->internal)
    app->internal->shutdown_callback = callback;
}

bool ecewo_is_running(App *app) {
  return app && app->internal ? app->internal->running : false;
}

int ecewo_active_connections(App *app) {
  return app && app->internal ? app->internal->active_connections : 0;
}

int server_pending_async_work(App *app) {
  if (!app || !app->internal)
    return 0;
  return (int)atomic_load_explicit(&app->internal->async_work_count,
                                   memory_order_acquire);
}

uv_loop_t *ecewo_get_loop(void) {
  return g_srv ? g_srv->loop : NULL;
}

static void timer_callback(uv_timer_t *handle) {
  timer_data_t *data = (timer_data_t *)handle->data;

  if (data && data->callback)
    data->callback(data->user_data);

  if (data && !data->is_interval) {
    uv_timer_stop(handle);
    uv_close((uv_handle_t *)handle, (uv_close_cb)free);
    free(data);
  }
}

Timer *ecewo_set_timeout(timer_callback_t callback, uint64_t delay_ms, void *user_data) {
  if (!g_srv || !g_srv->initialized || !callback)
    return NULL;

  Timer *timer = malloc(sizeof(Timer));
  timer_data_t *data = malloc(sizeof(timer_data_t));

  if (!timer || !data) {
    free(timer);
    free(data);
    return NULL;
  }

  data->callback = callback;
  data->user_data = user_data;
  data->is_interval = false;

  if (uv_timer_init(g_srv->loop, timer) != 0) {
    free(timer);
    free(data);
    return NULL;
  }

  timer->data = data;

  if (uv_timer_start(timer, timer_callback, delay_ms, 0) != 0) {
    free(timer);
    free(data);
    return NULL;
  }

  return timer;
}

Timer *ecewo_set_interval(timer_callback_t callback, uint64_t interval_ms, void *user_data) {
  if (!g_srv || !g_srv->initialized || !callback)
    return NULL;

  Timer *timer = malloc(sizeof(Timer));
  timer_data_t *data = malloc(sizeof(timer_data_t));

  if (!timer || !data) {
    free(timer);
    free(data);
    return NULL;
  }

  data->callback = callback;
  data->user_data = user_data;
  data->is_interval = true;

  if (uv_timer_init(g_srv->loop, timer) != 0) {
    free(timer);
    free(data);
    return NULL;
  }

  timer->data = data;

  if (uv_timer_start(timer, timer_callback, interval_ms, interval_ms) != 0) {
    free(timer);
    free(data);
    return NULL;
  }

  return timer;
}

void ecewo_clear_timer(Timer *timer) {
  if (!timer)
    return;

  uv_timer_stop(timer);

  timer_data_t *data = (timer_data_t *)timer->data;
  if (data) {
    free(data);
    timer->data = NULL;
  }

  uv_close((uv_handle_t *)timer, (uv_close_cb)free);
}

bool ecewo_client_is_valid(void *client_socket_data) {
  if (!client_socket_data)
    return false;

  client_t *client = (client_t *)client_socket_data;

  if (!client->valid || client->closing)
    return false;

  if (uv_is_closing((uv_handle_t *)&client->handle))
    return false;

  return true;
}
