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

struct ecewo_takeover_config_s {
  void *alloc_cb;
  void *read_cb;
  void *close_cb;
  void *user_data;
};

// Process-singleton runtime. Lazily initialized on the first ecewo_create();
// owns the shared event loop, signal handlers, and the registry of apps.
static ecewo__runtime_t ecewo_runtime;

ecewo__runtime_t *ecewo__runtime_get(void) {
  return &ecewo_runtime;
}

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
  ecewo_timer_cb_t callback;
  void *user_data;
  bool is_interval;
} timer_data_t;

static int runtime_ensure_initialized(void);
static void runtime_cleanup(ecewo__runtime_t *rt);
static void runtime_register_app(ecewo__runtime_t *rt, ecewo_app_t *app);
static void runtime_close_handles(ecewo__runtime_t *rt);
static void server_destroy(ecewo__server_t *srv);
static void server_shutdown_listener(ecewo__server_t *srv);

static void client_free_server(ecewo_client_t *client) {
  if (!client)
    return;
  if (client->connection_arena)
    ecewo_arena_return(client->connection_arena);
  free(client); // ref-counted; freed here when the count reaches zero
}

void ecewo_client_ref(ecewo_client_t *client) {
  if (!client)
    return;
  atomic_fetch_add_explicit(&client->refcount, 1, memory_order_relaxed);
}

void ecewo_client_unref(ecewo_client_t *client) {
  if (!client)
    return;
  int prev = atomic_fetch_sub_explicit(&client->refcount, 1, memory_order_acq_rel);
  if (prev <= 1)
    client_free_server(client);
}

static void add_ecewo_client_to_list(ecewo__server_t *srv, ecewo_client_t *client) {
  client->next = srv->client_list_head;
  srv->client_list_head = client;
}

static void remove_client_from_list(ecewo__server_t *srv, ecewo_client_t *client) {
  if (!client)
    return;

  if (srv->client_list_head == client) {
    srv->client_list_head = client->next;
    return;
  }

  ecewo_client_t *current = srv->client_list_head;
  while (current && current->next != client) {
    current = current->next;
  }

  if (current)
    current->next = client->next;
}

static void on_client_closed(uv_handle_t *handle) {
  ecewo_client_t *client = (ecewo_client_t *)handle->data;
  if (!client)
    return;

  ecewo__server_t *srv = client->srv;
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
  ecewo_client_t *client = (ecewo_client_t *)req->data;
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

  ecewo_client_unref(client); // drain reference
}

static void close_client(ecewo_client_t *client) {
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
    // Freed in on_drain_shutdown callback once the write side drains.
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
  ecewo__server_t *srv = (ecewo__server_t *)handle->data;
  if (!srv || srv->shutdown_requested)
    return;

  uint64_t idle_timeout = srv->app->idle_timeout_ms;
  uv_loop_t *loop = srv->runtime ? srv->runtime->loop : NULL;
  uint64_t now = loop ? uv_now(loop) : 0;
  ecewo_client_t *current = srv->client_list_head;

  while (current) {
    ecewo_client_t *next = current->next;

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

static int start_cleanup_timer(ecewo__server_t *srv) {
  if (!srv || !srv->runtime || !srv->runtime->loop)
    return -1;

  // libuv handle; freed via uv_close(handle, (uv_close_cb)free).
  srv->cleanup_timer = malloc(sizeof(uv_timer_t));
  if (!srv->cleanup_timer)
    return -1;

  if (uv_timer_init(srv->runtime->loop, srv->cleanup_timer) != 0) {
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

static void stop_cleanup_timer(ecewo__server_t *srv) {
  if (srv->cleanup_timer) {
    uv_timer_stop(srv->cleanup_timer);
    uv_close((uv_handle_t *)srv->cleanup_timer, (uv_close_cb)free);
    srv->cleanup_timer = NULL;
  }
}

void ecewo_increment_async_work(void) {
  ecewo__runtime_t *rt = &ecewo_runtime;
  if (!rt->initialized)
    return;

  uint_fast32_t prev = atomic_fetch_add_explicit(
      &rt->async_work_count, 1, memory_order_relaxed);

  if (prev == 0)
    uv_ref((uv_handle_t *)&rt->async_work_handle);
}

void ecewo_decrement_async_work(void) {
  ecewo__runtime_t *rt = &ecewo_runtime;
  if (!rt->initialized)
    return;

  uint_fast32_t prev = atomic_fetch_sub_explicit(
      &rt->async_work_count, 1, memory_order_acq_rel);

  if (prev == 0) {
    LOG_ERROR("Async work counter underflow!");
    atomic_store_explicit(&rt->async_work_count, 0, memory_order_release);
    return;
  }

  if (prev == 1)
    uv_unref((uv_handle_t *)&rt->async_work_handle);
}

static int client_connection_init(ecewo_client_t *client) {
  if (!client)
    return -1;

  client->connection_arena = ecewo_arena_borrow();
  if (!client->connection_arena)
    return -1;

  return 0;
}

static void client_parser_init(ecewo_client_t *client) {
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

static void client_context_init(ecewo_client_t *client) {
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

static void client_context_reset(ecewo_client_t *client) {
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
    free(handle->data); // timer_data_t stored by ecewo_timeout/ecewo_interval
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
  ecewo__server_t *srv = (ecewo__server_t *)handle->data;
  free(handle); // tcp_server libuv handle; freed here after uv_close completes
  if (srv) {
    srv->tcp_server = NULL;
    srv->server_closed = true;
  }
}

static void on_async_work_noop(uv_async_t *handle) {
  (void)handle;
}

static void on_force_close_timeout(uv_timer_t *handle) {
  ecewo__server_t *srv = (ecewo__server_t *)handle->data;
  uv_timer_stop(handle);
  uv_close((uv_handle_t *)handle, (uv_close_cb)free);
  if (!srv)
    return;

  srv->force_close_timer = NULL;

  ecewo_client_t *current = srv->client_list_head;
  while (current) {
    ecewo_client_t *next = current->next;
    if (!current->closing && !uv_is_closing((uv_handle_t *)&current->handle))
      close_client(current);
    current = next;
  }
}

// Per-app shutdown: stop the listener, close idle connections, arm a force-close
// timer for in-progress ones, fire the atexit callback, and unregister the app
// from the runtime. Safe to call from inside a request handler. Does not run
// any nested uv_run; the loop simply exits naturally once the app's handles
// drain and any other registered apps have also shut down.
void ecewo_shutdown(ecewo_app_t *app) {
  if (!app || !app->server)
    return;

  ecewo__server_t *srv = app->server;
  ecewo__runtime_t *rt = srv->runtime;

  if (srv->shutdown_requested)
    return;

  srv->shutdown_requested = true;
  srv->running = false;

  server_shutdown_listener(srv);

  // Unref cleanup timer so it no longer keeps the loop alive.
  // stop_cleanup_timer() in server_destroy() will fully close it.
  if (srv->cleanup_timer)
    uv_unref((uv_handle_t *)srv->cleanup_timer);

  // Close idle connections immediately; in-progress ones close themselves
  // when their request finishes. The force-close timer is the backstop.
  ecewo_client_t *current = srv->client_list_head;
  while (current) {
    ecewo_client_t *next = current->next;
    if (!current->request_in_progress && !current->closing)
      close_client(current);
    current = next;
  }

  // Arm a hard timeout as backstop for connections that take too long.
  // on_client_closed() cancels it early once all connections drain.
  if (srv->active_connections > 0 && rt && rt->loop) {
    // libuv handle; freed via uv_close in on_client_closed once all connections drain.
    srv->force_close_timer = malloc(sizeof(uv_timer_t));
    if (srv->force_close_timer) {
      uv_timer_init(rt->loop, srv->force_close_timer);
      srv->force_close_timer->data = srv;
      uv_timer_start(srv->force_close_timer, on_force_close_timeout,
                     srv->app->shutdown_timeout_ms, 0);
    }
  }

  // Fire the per-app atexit callback while the loop is still valid.
  // Safe here because we don't tear down the loop ourselves.
  if (srv->atexit_cb) {
    void (*cb)(void *) = srv->atexit_cb;
    void *ud = srv->atexit_user_data;
    srv->atexit_cb = NULL;
    srv->atexit_user_data = NULL;
    cb(ud);
  }

  // Mark this app as no longer live. When the last live app shuts down, close
  // runtime-level handles so the loop can drain and ecewo_run() can return.
  if (rt && srv->registered) {
    srv->registered = false;
    if (rt->live_app_count > 0)
      rt->live_app_count--;
    if (rt->live_app_count == 0)
      runtime_close_handles(rt);
  }

  // Return immediately. The outer uv_run() in ecewo_run() exits naturally
  // once all apps' handles + async_work_handle close or unref.
}

static void server_shutdown_listener(ecewo__server_t *srv) {
  if (!srv)
    return;

  if (srv->tcp_server && !uv_is_closing((uv_handle_t *)srv->tcp_server)) {
    srv->tcp_server->data = srv;
    uv_close((uv_handle_t *)srv->tcp_server, on_server_closed);
  }
}

#ifdef ECEWO_DEBUG
static void inspect_loop(uv_loop_t *loop);
#endif

// Frees per-app resources after the loop has exited. Must be called only when
// the event loop is no longer running, since it returns the app arena.
static void server_destroy(ecewo__server_t *srv) {
  if (!srv || !srv->initialized)
    return;

  // Fully close the cleanup timer (was only unref'd in ecewo_shutdown).
  stop_cleanup_timer(srv);

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

  if (srv->tcp_server && !srv->server_closed) {
    free(srv->tcp_server);
    srv->tcp_server = NULL;
  }

  srv->initialized = false;
}

static void runtime_shutdown_all_apps(ecewo__runtime_t *rt) {
  if (!rt)
    return;

  // The apps[] list never shrinks; ecewo_shutdown is idempotent (early-returns
  // when shutdown_requested is already set), so a straight walk is safe.
  for (size_t i = 0; i < rt->app_count; i++) {
    ecewo_app_t *app = rt->apps[i];
    if (app)
      ecewo_shutdown(app);
  }
}

static void runtime_close_handles(ecewo__runtime_t *rt) {
  if (!rt || rt->runtime_handles_closed)
    return;

  if (!uv_is_closing((uv_handle_t *)&rt->shutdown_async))
    uv_close((uv_handle_t *)&rt->shutdown_async, NULL);

  if (rt->signals_installed) {
    if (!uv_is_closing((uv_handle_t *)&rt->sigint_handle)) {
      uv_signal_stop(&rt->sigint_handle);
      uv_close((uv_handle_t *)&rt->sigint_handle, NULL);
    }
    if (!uv_is_closing((uv_handle_t *)&rt->sigterm_handle)) {
      uv_signal_stop(&rt->sigterm_handle);
      uv_close((uv_handle_t *)&rt->sigterm_handle, NULL);
    }
    rt->signals_installed = false;
  }

  if (!uv_is_closing((uv_handle_t *)&rt->async_work_handle))
    uv_close((uv_handle_t *)&rt->async_work_handle, NULL);

  rt->runtime_handles_closed = true;
}

static void on_async_shutdown(uv_async_t *handle) {
  (void)handle;
  ecewo__runtime_t *rt = &ecewo_runtime;
  if (!rt->initialized)
    return;

  runtime_shutdown_all_apps(rt);
  runtime_close_handles(rt);
}

static void on_signal(uv_signal_t *handle, int signum) {
  ecewo__runtime_t *rt = (ecewo__runtime_t *)handle->data;
  if (!rt || rt->shutdown_requested)
    return;

#ifdef ECEWO_DEBUG
  const char *signal_name = (signum == SIGINT) ? "SIGINT" : "SIGTERM";
  LOG_DEBUG("Received %s, shutting down...", signal_name);
#else
  (void)signum;
#endif

  rt->shutdown_requested = true;
  uv_async_send(&rt->shutdown_async);
}

ecewo_takeover_config_t *ecewo_takeover_config_new(void) {
  return calloc(1, sizeof(ecewo_takeover_config_t));
}

void ecewo_takeover_config_free(ecewo_takeover_config_t *config) {
  free(config);
}

void ecewo_takeover_config_set_alloc_cb(ecewo_takeover_config_t *config, void *alloc_cb) {
  if (config)
    config->alloc_cb = alloc_cb;
}

void ecewo_takeover_config_set_read_cb(ecewo_takeover_config_t *config, void *read_cb) {
  if (config)
    config->read_cb = read_cb;
}

void ecewo_takeover_config_set_close_cb(ecewo_takeover_config_t *config, void *close_cb) {
  if (config)
    config->close_cb = close_cb;
}

void ecewo_takeover_config_set_user_data(ecewo_takeover_config_t *config, void *user_data) {
  if (config)
    config->user_data = user_data;
}

int ecewo_connection_takeover(ecewo_response_t *res, const ecewo_takeover_config_t *config) {
  if (!res || !res->ecewo__client_socket || !config) {
    LOG_ERROR("connection_takeover: Invalid arguments");
    return -1;
  }

  uv_tcp_t *handle = (uv_tcp_t *)res->ecewo__client_socket;
  ecewo_client_t *client = (ecewo_client_t *)handle->data;

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

void *ecewo_get_client_handle(ecewo_response_t *res) {
  return res ? res->ecewo__client_socket : NULL;
}

ecewo_client_t *ecewo_req_client(ecewo_request_t *req) {
  return req ? (ecewo_client_t *)req->ecewo__client_socket : NULL;
}

ecewo_client_t *ecewo_res_client(ecewo_response_t *res) {
  return res ? (ecewo_client_t *)res->ecewo__client_socket : NULL;
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
  ecewo_client_t *client = (ecewo_client_t *)handle->data;

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

static void stop_request_timer(ecewo_client_t *client) {
  if (!client || !client->request_timeout_timer)
    return;

  uv_timer_stop(client->request_timeout_timer);
}

int ecewo_timeout_request(ecewo_response_t *res, uint64_t timeout_ms) {
  if (!res || !res->ecewo__client_socket)
    return -1;

  uv_tcp_t *sock = (uv_tcp_t *)res->ecewo__client_socket;
  if (!sock->data)
    return -1;

  ecewo_client_t *client = (ecewo_client_t *)sock->data;

  if (!client || client->closing || !client->srv || !client->srv->runtime)
    return -1;

  if (client->request_timeout_timer) {
    return uv_timer_start(client->request_timeout_timer,
                          on_request_timeout,
                          timeout_ms,
                          0);
  }

  // libuv handle; freed via uv_close(handle, (uv_close_cb)free) in close_client.
  client->request_timeout_timer = malloc(sizeof(uv_timer_t));
  if (!client->request_timeout_timer)
    return -1;

  if (uv_timer_init(client->srv->runtime->loop, client->request_timeout_timer) != 0) {
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
  ecewo_client_t *client = (ecewo_client_t *)handle->data;

  if (!client || (client->closing && !client->draining)
      || (client->srv && client->srv->shutdown_requested)) {
    buf->base = NULL;
    buf->len = 0;
    return;
  }

  *buf = client->read_buf;
}

void server_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  ecewo_client_t *client = (ecewo_client_t *)stream->data;

  if (!client)
    return;

  ecewo__server_t *srv = client->srv;
  uv_loop_t *loop = (srv && srv->runtime) ? srv->runtime->loop : NULL;

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

  client->last_activity = loop ? uv_now(loop) : 0;

  if (!client->parser_initialized) {
    client_parser_init(client);
    client_context_init(client);
    client->request_in_progress = false;
  }

  if (!client->request_in_progress) {
    client_context_reset(client);
    client->request_in_progress = true;

    // Start per-request timeout if configured
    if (srv && srv->app && srv->app->request_timeout_ms > 0 && loop) {
      if (client->request_timeout_timer) {
        uv_timer_start(client->request_timeout_timer,
                       on_request_timeout,
                       srv->app->request_timeout_ms,
                       0);
      } else {
        // libuv handle; freed via uv_close(handle, (uv_close_cb)free) in close_client.
        client->request_timeout_timer = malloc(sizeof(uv_timer_t));
        if (client->request_timeout_timer) {
          if (uv_timer_init(loop, client->request_timeout_timer) == 0) {
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

  ecewo__server_t *srv = (ecewo__server_t *)server->data;
  if (!srv || !srv->runtime || !srv->runtime->loop)
    return;

  if (srv->shutdown_requested)
    return;

  if (srv->active_connections >= srv->app->max_connections) {
    LOG_DEBUG("Max connections (%d) reached", srv->app->max_connections);
    return;
  }

  // ref-counted; freed in client_free_server when the count reaches zero.
  ecewo_client_t *client = calloc(1, sizeof(ecewo_client_t));
  if (!client)
    return;

  client->valid = true;
  client->last_activity = uv_now(srv->runtime->loop);
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

  if (uv_tcp_init(srv->runtime->loop, &client->handle) != 0) {
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
      add_ecewo_client_to_list(srv, client);
      srv->active_connections++;
    } else {
      close_client(client);
    }
  } else {
    close_client(client);
  }
}

// ---------------------------------------------------------------------------
// Runtime singleton lifecycle
// ---------------------------------------------------------------------------

static int runtime_ensure_initialized(void) {
  ecewo__runtime_t *rt = &ecewo_runtime;
  if (rt->initialized)
    return 0;

  // Freed via uv_loop_close + free in runtime_cleanup.
  rt->loop = malloc(sizeof(uv_loop_t));
  if (!rt->loop)
    return -1;

  if (uv_loop_init(rt->loop) != 0) {
    free(rt->loop);
    rt->loop = NULL;
    return -1;
  }

  arena_pool_init();
  if (!arena_pool_is_initialized()) {
    LOG_ERROR("Arena pool initialization failed");
    uv_loop_close(rt->loop);
    free(rt->loop);
    rt->loop = NULL;
    return -1;
  }

  init_date_cache();

  if (uv_async_init(rt->loop, &rt->shutdown_async, on_async_shutdown) != 0) {
    arena_pool_destroy();
    destroy_date_cache();
    uv_loop_close(rt->loop);
    free(rt->loop);
    rt->loop = NULL;
    return -1;
  }
  rt->shutdown_async.data = rt;

  if (uv_async_init(rt->loop, &rt->async_work_handle, on_async_work_noop) != 0) {
    uv_close((uv_handle_t *)&rt->shutdown_async, NULL);
    while (uv_run(rt->loop, UV_RUN_NOWAIT) != 0)
      ;
    arena_pool_destroy();
    destroy_date_cache();
    uv_loop_close(rt->loop);
    free(rt->loop);
    rt->loop = NULL;
    return -1;
  }
  rt->async_work_handle.data = rt;
  // Unreffed by default; only reffed while async work is in flight
  uv_unref((uv_handle_t *)&rt->async_work_handle);
  atomic_init(&rt->async_work_count, 0);

  // Signal handlers - install once at runtime level
  rt->signals_installed = false;
  const char *is_worker = getenv("ECEWO_WORKER");
  bool in_cluster = (is_worker && strcmp(is_worker, "1") == 0);

  if (!in_cluster) {
    if (uv_signal_init(rt->loop, &rt->sigint_handle) == 0
        && uv_signal_init(rt->loop, &rt->sigterm_handle) == 0) {
      rt->sigint_handle.data = rt;
      rt->sigterm_handle.data = rt;
      uv_signal_start(&rt->sigint_handle, on_signal, SIGINT);
      uv_signal_start(&rt->sigterm_handle, on_signal, SIGTERM);
      rt->signals_installed = true;
    }
  }

  rt->apps = NULL;
  rt->app_count = 0;
  rt->app_capacity = 0;
  rt->live_app_count = 0;
  rt->running = false;
  rt->shutdown_requested = false;
  rt->runtime_handles_closed = false;
  rt->initialized = true;

  return 0;
}

static void runtime_register_app(ecewo__runtime_t *rt, ecewo_app_t *app) {
  if (!rt || !app || !app->server)
    return;

  if (rt->app_count == rt->app_capacity) {
    size_t new_cap = rt->app_capacity ? rt->app_capacity * 2 : 4;
    ecewo_app_t **tmp = realloc(rt->apps, new_cap * sizeof(*tmp));
    if (!tmp) {
      LOG_ERROR("Out of memory registering app");
      return;
    }
    rt->apps = tmp;
    rt->app_capacity = new_cap;
  }

  rt->apps[rt->app_count++] = app;
  rt->live_app_count++;
  app->server->registered = true;
}

static void runtime_cleanup(ecewo__runtime_t *rt) {
  if (!rt || !rt->initialized)
    return;

  // Final walk to close any handles that slipped through (timers, async, etc.).
  if (rt->loop) {
    uv_walk(rt->loop, close_walk_cb, NULL);
    while (uv_run(rt->loop, UV_RUN_DEFAULT) != 0)
      ;
  }

#ifdef ECEWO_DEBUG
  if (rt->loop)
    inspect_loop(rt->loop);
#endif

  if (rt->loop) {
    int result = uv_loop_close(rt->loop);
    if (result != 0)
      LOG_ERROR("uv_loop_close failed: %s", uv_strerror(result));
    free(rt->loop);
    rt->loop = NULL;
  }

  arena_pool_destroy();
  destroy_date_cache();

  free(rt->apps);
  rt->apps = NULL;
  rt->app_count = 0;
  rt->app_capacity = 0;
  rt->live_app_count = 0;
  rt->running = false;
  rt->shutdown_requested = false;
  rt->signals_installed = false;
  rt->runtime_handles_closed = false;
  rt->initialized = false;
}

// Tears down every registered app's internal state and frees the app/server
// structs themselves. Caller pointers held to these apps become dangling and
// must not be used after ecewo_run() returns. Must run before runtime_cleanup()
// because server_destroy returns the app arena to the pool that runtime_cleanup
// then destroys.
static void runtime_destroy_all_apps(ecewo__runtime_t *rt) {
  if (!rt)
    return;
  for (size_t i = 0; i < rt->app_count; i++) {
    ecewo_app_t *a = rt->apps[i];
    if (!a)
      continue;
    if (a->server) {
      server_destroy(a->server);
      free(a->server);
      a->server = NULL;
    }
    free(a);
    rt->apps[i] = NULL;
  }
}

static void global_runtime_atexit(void) {
  ecewo__runtime_t *rt = &ecewo_runtime;
  if (!rt->initialized)
    return;

  // If the user never called ecewo_run() (or it never returned cleanly), do
  // best-effort cleanup of any registered apps before tearing down the loop.
  runtime_shutdown_all_apps(rt);
  runtime_close_handles(rt);

  if (rt->loop) {
    uv_walk(rt->loop, close_walk_cb, NULL);
    while (uv_run(rt->loop, UV_RUN_DEFAULT) != 0)
      ;
  }

  runtime_destroy_all_apps(rt);
  runtime_cleanup(rt);
}

// ---------------------------------------------------------------------------
// Public app lifecycle
// ---------------------------------------------------------------------------

ecewo_app_t *ecewo_create(void) {
  ecewo__runtime_t *rt = &ecewo_runtime;

  bool was_initialized = rt->initialized;
  if (runtime_ensure_initialized() != 0) {
    LOG_ERROR("Runtime initialization failed");
    return NULL;
  }
  if (!was_initialized)
    atexit(global_runtime_atexit);

  // Root struct; no arena exists yet to allocate from.
  ecewo_app_t *app = calloc(1, sizeof(ecewo_app_t));
  if (!app)
    return NULL;

  app->max_connections = 10000;
  app->listen_backlog = 511;
  app->idle_timeout_ms = 60000;
  app->request_timeout_ms = 0;
  app->cleanup_interval_ms = 30000;
  app->shutdown_timeout_ms = 15000;

  // Root struct; no arena exists yet to allocate from.
  ecewo__server_t *srv = calloc(1, sizeof(ecewo__server_t));
  if (!srv) {
    free(app);
    return NULL;
  }

  srv->app = app;
  srv->runtime = rt;
  app->server = srv;

  app->arena = ecewo_arena_borrow();
  if (!app->arena) {
    LOG_ERROR("App arena allocation failed");
    free(srv);
    free(app);
    return NULL;
  }

  srv->route_table = route_table_create(app->arena);
  if (!srv->route_table) {
    LOG_ERROR("Failed to create route table");
    ecewo_arena_return(app->arena);
    app->arena = NULL;
    free(srv);
    free(app);
    return NULL;
  }

  srv->initialized = true;

  runtime_register_app(rt, app);

  return app;
}

int ecewo_bind(ecewo_app_t *app, uint16_t port) {
  if (!app || !app->server)
    return SERVER_NOT_INITIALIZED;

  ecewo__server_t *srv = app->server;
  ecewo__runtime_t *rt = srv->runtime;

  if (port == 0) {
    LOG_ERROR("Invalid port %" PRIu16 " (must be 1-65535)", port);
    return SERVER_INVALID_PORT;
  }

  if (!srv->initialized || !rt || !rt->loop)
    return SERVER_NOT_INITIALIZED;

  if (srv->running)
    return SERVER_ALREADY_RUNNING;

  // libuv handle; freed in on_server_closed after uv_close completes.
  srv->tcp_server = malloc(sizeof(uv_tcp_t));
  if (!srv->tcp_server)
    return SERVER_OUT_OF_MEMORY;

  if (uv_tcp_init(rt->loop, srv->tcp_server) != 0) {
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

void ecewo_run(void) {
  ecewo__runtime_t *rt = &ecewo_runtime;

  if (!rt->initialized || !rt->loop) {
    LOG_ERROR("Runtime not initialized. Call ecewo_create() first");
    return;
  }

  // Require at least one app to have a listener bound; otherwise the loop
  // would just exit immediately and silently.
  bool any_running = false;
  for (size_t i = 0; i < rt->app_count; i++) {
    ecewo_app_t *a = rt->apps[i];
    if (a && a->server && a->server->running) {
      any_running = true;
      break;
    }
  }
  if (!any_running) {
    LOG_ERROR("No app has been bound: Call ecewo_bind() before ecewo_run()");
    return;
  }

  // If the runtime loop is already running on this thread (e.g. nested call
  // from a handler) just return. uv_run is not reentrant.
  if (rt->running)
    return;

  rt->running = true;
  uv_run(rt->loop, UV_RUN_DEFAULT);
  rt->running = false;

  // The loop has exited. Tear down every app's internal state, then the
  // runtime. App/server structs are intentionally not freed so the caller's
  // ecewo_app_t pointers remain readable (though using them is UB).
  runtime_destroy_all_apps(rt);
  runtime_cleanup(rt);
}

void ecewo_listen(ecewo_app_t *app, uint16_t port) {
  if (ecewo_bind(app, port) != 0)
    return;

  ecewo_run();
}

void ecewo_atexit(ecewo_app_t *app, void (*callback)(void *user_data), void *user_data) {
  if (!app || !app->server || !callback)
    return;
  app->server->atexit_cb = callback;
  app->server->atexit_user_data = user_data;
}

bool ecewo_is_running(ecewo_app_t *app) {
  return app && app->server ? app->server->running : false;
}

int ecewo_active_connections(ecewo_app_t *app) {
  return app && app->server ? app->server->active_connections : 0;
}

ecewo_arena_t *ecewo_app_arena(const ecewo_app_t *app) {
  return app ? app->arena : NULL;
}

void ecewo_set_app_data(ecewo_app_t *app, void *key, void *data) {
  if (!app || !key)
    return;
  for (int i = 0; i < app->plugin_slot_count; i++) {
    if (app->plugin_slots[i].key == key) {
      app->plugin_slots[i].data = data;
      return;
    }
  }
  if (app->plugin_slot_count >= app->plugin_slot_capacity) {
    int new_cap = app->plugin_slot_capacity == 0 ? 4 : app->plugin_slot_capacity * 2;
    app->plugin_slots = ecewo_realloc(app->arena, app->plugin_slots,
                                      (size_t)app->plugin_slot_capacity * sizeof(plugin_slot_t),
                                      (size_t)new_cap * sizeof(plugin_slot_t));
    app->plugin_slot_capacity = new_cap;
  }
  app->plugin_slots[app->plugin_slot_count].key = key;
  app->plugin_slots[app->plugin_slot_count].data = data;
  app->plugin_slot_count++;
}

void *ecewo_get_app_data(const ecewo_app_t *app, void *key) {
  if (!app || !key)
    return NULL;
  for (int i = 0; i < app->plugin_slot_count; i++) {
    if (app->plugin_slots[i].key == key)
      return app->plugin_slots[i].data;
  }
  return NULL;
}

int server_pending_async_work(ecewo_app_t *app) {
  (void)app;
  ecewo__runtime_t *rt = &ecewo_runtime;
  if (!rt->initialized)
    return 0;
  return (int)atomic_load_explicit(&rt->async_work_count, memory_order_acquire);
}

void *ecewo_get_loop(void) {
  ecewo__runtime_t *rt = &ecewo_runtime;
  return rt->initialized ? rt->loop : NULL;
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

ecewo_timer_t *ecewo_timeout(ecewo_timer_cb_t callback, uint64_t delay_ms, void *user_data) {
  ecewo__runtime_t *rt = &ecewo_runtime;
  if (!rt->initialized || !rt->loop || !callback)
    return NULL;

  // libuv handle; freed via uv_close(handle, (uv_close_cb)free) in timer_callback.
  // data freed in timer_callback after the handler runs.
  uv_timer_t *timer = malloc(sizeof(uv_timer_t));
  timer_data_t *data = malloc(sizeof(timer_data_t));

  if (!timer || !data) {
    free(timer);
    free(data);
    return NULL;
  }

  data->callback = callback;
  data->user_data = user_data;
  data->is_interval = false;

  if (uv_timer_init(rt->loop, timer) != 0) {
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

  return (ecewo_timer_t *)timer;
}

ecewo_timer_t *ecewo_interval(ecewo_timer_cb_t callback, uint64_t interval_ms, void *user_data) {
  ecewo__runtime_t *rt = &ecewo_runtime;
  if (!rt->initialized || !rt->loop || !callback)
    return NULL;

  // libuv handle; freed via uv_close in ecewo_clear_timer or when the loop drains.
  // data freed in ecewo_clear_timer.
  uv_timer_t *timer = malloc(sizeof(uv_timer_t));
  timer_data_t *data = malloc(sizeof(timer_data_t));

  if (!timer || !data) {
    free(timer);
    free(data);
    return NULL;
  }

  data->callback = callback;
  data->user_data = user_data;
  data->is_interval = true;

  if (uv_timer_init(rt->loop, timer) != 0) {
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

  return (ecewo_timer_t *)timer;
}

void ecewo_clear_timer(ecewo_timer_t *handle) {
  if (!handle)
    return;

  uv_timer_t *timer = (uv_timer_t *)handle;
  uv_timer_stop(timer);

  timer_data_t *data = (timer_data_t *)timer->data;
  if (data) {
    free(data);
    timer->data = NULL;
  }

  uv_close((uv_handle_t *)timer, (uv_close_cb)free);
}

bool ecewo_client_is_valid(ecewo_client_t *client) {
  if (!client)
    return false;

  if (!client->valid || client->closing)
    return false;

  if (uv_is_closing((uv_handle_t *)&client->handle))
    return false;

  return true;
}

// ---------------------------------------------------------------------------
// App configuration setters
// ---------------------------------------------------------------------------

void ecewo_set_max_connections(ecewo_app_t *app, int val) {
  if (app)
    app->max_connections = val;
}
void ecewo_set_listen_backlog(ecewo_app_t *app, int val) {
  if (app)
    app->listen_backlog = val;
}
void ecewo_set_idle_timeout(ecewo_app_t *app, uint64_t ms) {
  if (app)
    app->idle_timeout_ms = ms;
}
void ecewo_set_request_timeout(ecewo_app_t *app, uint64_t ms) {
  if (app)
    app->request_timeout_ms = ms;
}
void ecewo_set_cleanup_interval(ecewo_app_t *app, uint64_t ms) {
  if (app)
    app->cleanup_interval_ms = ms;
}
void ecewo_set_shutdown_timeout(ecewo_app_t *app, uint64_t ms) {
  if (app)
    app->shutdown_timeout_ms = ms;
}
