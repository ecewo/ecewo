#include <stdlib.h>
#include <signal.h>
#include <inttypes.h>
#include "server.h"
#include "route-trie.h"
#include "middleware.h"
#include "router.h"
#include "arena.h"
#include "utils.h"
#include "logger.h"

static atomic_uint_fast64_t next_client_id = ATOMIC_VAR_INIT(1);

#ifndef MAX_CONNECTIONS
#define MAX_CONNECTIONS 10000
#endif

#ifndef LISTEN_BACKLOG
#define LISTEN_BACKLOG 511
#endif

#ifndef IDLE_TIMEOUT_MS
#define IDLE_TIMEOUT_MS 60000
#endif

#ifndef REQUEST_TIMEOUT_MS
#define REQUEST_TIMEOUT_MS 0 /* disabled by default, configure via CMake */
#endif

#ifndef CLEANUP_INTERVAL_MS
#define CLEANUP_INTERVAL_MS 30000
#endif

#ifndef SHUTDOWN_TIMEOUT_MS
#define SHUTDOWN_TIMEOUT_MS 15000
#endif

#ifndef CLEANUP_TIMEOUT_MS
#define CLEANUP_TIMEOUT_MS 5000
#endif

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

static struct {
  bool initialized;
  bool running;
  bool shutdown_requested;
  int active_connections;
  atomic_uint_fast16_t pending_async_work;

  uv_loop_t *loop;
  uv_tcp_t *server;
  uv_signal_t sigint_handle;
  uv_signal_t sigterm_handle;
  uv_async_t shutdown_async;

  shutdown_callback_t shutdown_callback;

  client_t *client_list_head;
  uv_timer_t *cleanup_timer;

  bool server_closed;
} ecewo_server = { 0 };

route_trie_t *global_route_trie = NULL;

static void client_free_internal(client_t *client) {
  if (!client)
    return;
  if (client->connection_arena)
    arena_return(client->connection_arena);
  free(client);
}

void client_ref(client_t *client) {
  if (!client)
    return;
  atomic_fetch_add_explicit(&client->refcount, 1, memory_order_relaxed);
}

void client_unref(client_t *client) {
  if (!client)
    return;
  int prev = atomic_fetch_sub_explicit(&client->refcount, 1, memory_order_acq_rel);
  if (prev <= 1)
    client_free_internal(client);
}

static void add_client_to_list(client_t *client) {
  client->next = ecewo_server.client_list_head;
  ecewo_server.client_list_head = client;
}

static void remove_client_from_list(client_t *client) {
  if (!client)
    return;

  if (ecewo_server.client_list_head == client) {
    ecewo_server.client_list_head = client->next;
    return;
  }

  client_t *current = ecewo_server.client_list_head;
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

  remove_client_from_list(client);
  if (ecewo_server.active_connections > 0)
    ecewo_server.active_connections--;

  client->valid = false;

  client_unref(client);
}

static void close_client(client_t *client) {
  if (!client || client->closing)
    return;
  
  bool need_unref = (client->request_timeout_timer != NULL);
  
  if (client->request_timeout_timer) {
    uv_timer_stop(client->request_timeout_timer);
    uv_close((uv_handle_t *)client->request_timeout_timer, (uv_close_cb)free);
    client->request_timeout_timer = NULL;
  }
  
  client->closing = true;
  client->valid = false;
  
  uv_read_stop((uv_stream_t *)&client->handle);
  
  if (!uv_is_closing((uv_handle_t *)&client->handle))
    uv_close((uv_handle_t *)&client->handle, on_client_closed);
  
  if (need_unref)
    client_unref(client);
}

static void cleanup_idle_connections(uv_timer_t *handle) {
  (void)handle;

  if (ecewo_server.shutdown_requested)
    return;

  uint64_t now = uv_now(ecewo_server.loop);
  client_t *current = ecewo_server.client_list_head;

  while (current) {
    client_t *next = current->next;

    if (current->taken_over) {
      current = next;
      continue;
    }

    if (current->keep_alive_enabled && !current->closing) {
      uint64_t idle_time = now - current->last_activity;

      if (idle_time > IDLE_TIMEOUT_MS)
        close_client(current);
    }
    current = next;
  }
}

static int start_cleanup_timer(void) {
  ecewo_server.cleanup_timer = malloc(sizeof(uv_timer_t));
  if (!ecewo_server.cleanup_timer)
    return -1;

  if (uv_timer_init(ecewo_server.loop, ecewo_server.cleanup_timer) != 0) {
    free(ecewo_server.cleanup_timer);
    ecewo_server.cleanup_timer = NULL;
    return -1;
  }

  if (uv_timer_start(ecewo_server.cleanup_timer, cleanup_idle_connections,
                     CLEANUP_INTERVAL_MS, CLEANUP_INTERVAL_MS)
      != 0) {
    uv_close((uv_handle_t *)ecewo_server.cleanup_timer, (uv_close_cb)free);
    ecewo_server.cleanup_timer = NULL;
    return -1;
  }

  return 0;
}

static void stop_cleanup_timer(void) {
  if (ecewo_server.cleanup_timer) {
    uv_timer_stop(ecewo_server.cleanup_timer);
    uv_close((uv_handle_t *)ecewo_server.cleanup_timer, (uv_close_cb)free);
    ecewo_server.cleanup_timer = NULL;
  }
}

void increment_async_work(void) {
  uint_fast16_t new_val = atomic_fetch_add_explicit(
                              &ecewo_server.pending_async_work,
                              1,
                              memory_order_relaxed)
      + 1;

#ifdef ECEWO_DEBUG
  LOG_DEBUG("Async work count: %" PRIuFAST16, new_val);
#endif

  (void)new_val;
}

void decrement_async_work(void) {
  uint_fast16_t prev = atomic_fetch_sub_explicit(
      &ecewo_server.pending_async_work,
      1,
      memory_order_acq_rel);

  if (prev == 0) {
    LOG_ERROR("Async work counter underflow!");
    atomic_store_explicit(&ecewo_server.pending_async_work, 0, memory_order_release);
    return;
  }

  if (prev == 1 && ecewo_server.shutdown_requested) {
    uv_async_send(&ecewo_server.shutdown_async);
  }
}

int get_pending_async_work(void) {
  return (int)atomic_load_explicit(
      &ecewo_server.pending_async_work,
      memory_order_acquire);
}

static int client_connection_init(client_t *client) {
  if (!client)
    return -1;

  client->connection_arena = arena_borrow();
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

  arena_reset(client->connection_arena);

  llhttp_reset(&client->persistent_parser);

  http_context_init(&client->persistent_context,
                    client->connection_arena,
                    &client->persistent_parser,
                    &client->persistent_settings);
}

static void close_walk_cb(uv_handle_t *handle, void *arg) {
  (void)arg;

  if (uv_is_closing(handle))
    return;

  if (handle->type == UV_TCP) {
    if (handle != (uv_handle_t *)ecewo_server.server) {
      client_t *client = (client_t *)handle->data;
      if (client)
        close_client(client);
    }
    // } else if (handle->type == UV_TIMER) {
    //   if (handle != (uv_handle_t *)ecewo_server.cleanup_timer) {
    //     uv_timer_stop((uv_timer_t *)handle);
    //     timer_data_t *data = (timer_data_t *)handle->data;
    //     if (data)
    //       free(data);
    //     uv_close(handle, (uv_close_cb)free);
    //   }
  } else if (handle->type == UV_SIGNAL) {
    uv_signal_stop((uv_signal_t *)handle);
    uv_close(handle, NULL);
  } else {
    uv_close(handle, NULL);
  }
}

static void on_server_closed(uv_handle_t *handle) {
  (void)handle;
  if (ecewo_server.server) {
    free(ecewo_server.server);
    ecewo_server.server = NULL;
    ecewo_server.server_closed = 1;
  }
}

void server_shutdown(void) {
  if (ecewo_server.shutdown_requested)
    return;

  ecewo_server.shutdown_requested = 1;
  ecewo_server.running = 0;

  if (ecewo_server.shutdown_callback)
    ecewo_server.shutdown_callback();

  stop_cleanup_timer();

  const char *is_worker = getenv("ECEWO_WORKER");
  bool in_cluster = (is_worker && strcmp(is_worker, "1") == 0);

  if (!in_cluster) {
    if (!uv_is_closing((uv_handle_t *)&ecewo_server.sigint_handle)) {
      uv_signal_stop(&ecewo_server.sigint_handle);
      uv_close((uv_handle_t *)&ecewo_server.sigint_handle, NULL);
    }

    if (!uv_is_closing((uv_handle_t *)&ecewo_server.sigterm_handle)) {
      uv_signal_stop(&ecewo_server.sigterm_handle);
      uv_close((uv_handle_t *)&ecewo_server.sigterm_handle, NULL);
    }
  }

  if (ecewo_server.server && !uv_is_closing((uv_handle_t *)ecewo_server.server))
    uv_close((uv_handle_t *)ecewo_server.server, on_server_closed);

  client_t *current = ecewo_server.client_list_head;
  while (current) {
    client_t *next = current->next;
    close_client(current);
    current = next;
  }

  // STEP 1: Wait for external async work (Postgres, Redis, etc.)
  uint64_t start = uv_now(ecewo_server.loop);

  while (get_pending_async_work() > 0) {
    if ((uv_now(ecewo_server.loop) - start) >= SHUTDOWN_TIMEOUT_MS) {
      LOG_DEBUG("External async timeout: %d operations abandoned",
                get_pending_async_work());
      break;
    }
    uv_run(ecewo_server.loop, UV_RUN_ONCE);
  }

  // STEP 2: Close all remaining handles
  uv_walk(ecewo_server.loop, close_walk_cb, NULL);

  // STEP 3: Wait for libuv cleanup (timers, work queue, etc.)
  start = uv_now(ecewo_server.loop);

  while (uv_loop_alive(ecewo_server.loop)) {
    if ((uv_now(ecewo_server.loop) - start) >= SHUTDOWN_TIMEOUT_MS) {
      LOG_DEBUG("libuv cleanup timeout, forcing close");
      break;
    }
    uv_run(ecewo_server.loop, UV_RUN_ONCE);
  }

  // STEP 4: Cleanup taken-over connections
  // Some clients may have been taken over by plugins (e.g., WebSocket)
  // Their handles are already closed, but client structs are still in the list
  current = ecewo_server.client_list_head;
  while (current) {
    client_t *next = current->next;

    if (uv_is_closing((uv_handle_t *)&current->handle) && !current->closing) {
      remove_client_from_list(current);
      ecewo_server.active_connections--;

      if (current->connection_arena)
        arena_return(current->connection_arena);

      free(current);
    }

    current = next;
  }

  if (ecewo_server.active_connections > 0)
    LOG_DEBUG("Warning: %d connections not properly closed",
              ecewo_server.active_connections);
}

static void router_cleanup(void) {
  if (global_route_trie) {
    // Middleware contexts will be cleaned up in route_trie_free
    route_trie_free(global_route_trie);
    global_route_trie = NULL;
  }

  reset_middleware();
}

int connection_takeover(Res *res, const TakeoverConfig *config) {
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

uv_tcp_t *get_client_handle(Res *res) {
  return res ? res->client_socket : NULL;
}

static void server_cleanup(void) {
  if (!ecewo_server.initialized)
    return;

  if (!ecewo_server.shutdown_requested)
    server_shutdown();

  stop_cleanup_timer();
  router_cleanup();
  arena_pool_destroy();
  destroy_date_cache();

  uint64_t start = uv_now(ecewo_server.loop);

  while (uv_loop_alive(ecewo_server.loop)) {
    if ((uv_now(ecewo_server.loop) - start) >= CLEANUP_TIMEOUT_MS) {
      LOG_DEBUG("Cleanup timeout: forcing loop close");
      break;
    }

    uv_run(ecewo_server.loop, UV_RUN_NOWAIT);
  }

  int result = uv_loop_close(ecewo_server.loop);
  if (result != 0) {
    uv_walk(ecewo_server.loop, close_walk_cb, NULL);
    uv_run(ecewo_server.loop, UV_RUN_NOWAIT);
    uv_loop_close(ecewo_server.loop);
  }

  if (ecewo_server.server && !ecewo_server.server_closed)
    free(ecewo_server.server);

  memset(&ecewo_server, 0, sizeof(ecewo_server));
}

static void on_async_shutdown(uv_async_t *handle) {
  (void)handle;
  server_shutdown();
}

static void on_signal(uv_signal_t *handle, int signum) {
  (void)handle;

  if (ecewo_server.shutdown_requested)
    return;

#ifdef ECEWO_DEBUG
  const char *signal_name = (signum == SIGINT) ? "SIGINT" : "SIGTERM";
  LOG_DEBUG("Received %s, shutting down...", signal_name);
#else
  (void)signum;
#endif

  uv_async_send(&ecewo_server.shutdown_async);
}

static int router_init(void) {
  if (global_route_trie)
    return 0;

  global_route_trie = route_trie_create();
  if (!global_route_trie) {
    LOG_ERROR("Failed to create route trie");
    return 1;
  }

  return 0;
}

int server_init(void) {
  if (ecewo_server.initialized)
    return SERVER_ALREADY_INITIALIZED;

  memset(&ecewo_server, 0, sizeof(ecewo_server));

  ecewo_server.loop = uv_default_loop();
  if (!ecewo_server.loop)
    return SERVER_INIT_FAILED;

  arena_pool_init();

  if (!arena_pool_is_initialized()) {
    LOG_ERROR("Arena pool initialization failed");
    return SERVER_INIT_FAILED;
  }

  init_date_cache();

  const char *is_worker = getenv("ECEWO_WORKER");
  bool in_cluster = (is_worker && strcmp(is_worker, "1") == 0);

  if (!in_cluster) {
    if (uv_signal_init(ecewo_server.loop, &ecewo_server.sigint_handle) != 0 || uv_signal_init(ecewo_server.loop, &ecewo_server.sigterm_handle) != 0) {
      return SERVER_INIT_FAILED;
    }

    uv_signal_start(&ecewo_server.sigint_handle, on_signal, SIGINT);
    uv_signal_start(&ecewo_server.sigterm_handle, on_signal, SIGTERM);
  }

  if (uv_async_init(ecewo_server.loop, &ecewo_server.shutdown_async, on_async_shutdown) != 0)
    return SERVER_INIT_FAILED;

  atomic_store_explicit(&ecewo_server.pending_async_work, 0, memory_order_relaxed);

  if (router_init() != 0)
    return SERVER_INIT_FAILED;

  ecewo_server.initialized = 1;
  atexit(server_cleanup);

  return SERVER_OK;
}

static void on_request_timeout(uv_timer_t *handle) {
  client_t *client = (client_t *)handle->data;

  LOG_ERROR("Request timeout - closing connection");

  if (client) {
    if (client->connection_arena)
      arena_reset(client->connection_arena);
    
    client->request_timeout_timer = NULL;
    close_client(client);
    client_unref(client);
  }
  
  uv_timer_stop(handle);
  uv_close((uv_handle_t *)handle, (uv_close_cb)free);
}

static bool stop_request_timer(client_t *client) {
  if (!client || !client->request_timeout_timer)
    return false;
  
  uv_timer_stop(client->request_timeout_timer);
  uv_close((uv_handle_t *)client->request_timeout_timer, (uv_close_cb)free);
  client->request_timeout_timer = NULL;
  
  return true;
}

int request_timeout(Res *res, uint64_t timeout_ms) {
  if (!res || !res->client_socket || !res->client_socket->data)
    return -1;
  
  client_t *client = (client_t *)res->client_socket->data;
  
  if (!client || client->closing)
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
  
  if (uv_timer_init(ecewo_server.loop, client->request_timeout_timer) != 0) {
    free(client->request_timeout_timer);
    client->request_timeout_timer = NULL;
    return -1;
  }
  
  client->request_timeout_timer->data = client;
  client_ref(client);
  
  if (uv_timer_start(client->request_timeout_timer,
                     on_request_timeout,
                     timeout_ms,
                     0) != 0) {
    client_unref(client);
    uv_close((uv_handle_t *)client->request_timeout_timer, (uv_close_cb)free);
    client->request_timeout_timer = NULL;
    return -1;
  }
  
  return 0;
}

void server_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  (void)suggested_size;
  client_t *client = (client_t *)handle->data;

  if (!client || client->closing || ecewo_server.shutdown_requested) {
    buf->base = NULL;
    buf->len = 0;
    return;
  }

  *buf = client->read_buf;
}

void server_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  client_t *client = (client_t *)stream->data;

  if (!client || client->closing)
    return;

  if (ecewo_server.shutdown_requested) {
    close_client(client);
    return;
  }

  if (nread < 0) {
    close_client(client);
    return;
  }

  if (nread == 0)
    return; // EAGAIN/EWOULDBLOCK

  client->last_activity = uv_now(ecewo_server.loop);

  // Initialize parser only once per connection
  if (!client->parser_initialized) {
    client_parser_init(client);
    client_context_init(client);
    client->request_in_progress = false;
  }

  // Only reset context when starting a NEW request
  // Don't reset if we're continuing a partial request
  if (!client->request_in_progress) {
    client_context_reset(client);
    client->request_in_progress = true;

    #if REQUEST_TIMEOUT_MS > 0
      if (!client->request_timeout_timer) {
        client->request_timeout_timer = malloc(sizeof(uv_timer_t));
        if (client->request_timeout_timer) {
          if (uv_timer_init(ecewo_server.loop, client->request_timeout_timer) == 0) {
            client->request_timeout_timer->data = client;
            client_ref(client);
            
            if (uv_timer_start(client->request_timeout_timer,
                              on_request_timeout,
                              REQUEST_TIMEOUT_MS,
                              0) != 0) {
              client_unref(client);
              uv_close((uv_handle_t *)client->request_timeout_timer, (uv_close_cb)free);
              client->request_timeout_timer = NULL;
            }
          } else {
            free(client->request_timeout_timer);
            client->request_timeout_timer = NULL;
          }
        }
      }
    #endif
  }

  if (buf && buf->base) {
    int result = router(client, buf->base, (size_t)nread);

    switch (result) {
    case REQUEST_KEEP_ALIVE:
      // The timer is stopped for async responses by write_completion_cb in response.c
      bool had_timer = stop_request_timer(client);
      client->keep_alive_enabled = true;
      client->request_in_progress = false;
      if (had_timer)
        client_unref(client);
      break;

    case REQUEST_CLOSE:
      close_client(client);
      break;

    case REQUEST_PENDING:
      // It may be PARSE_INCOMPLETE, need to wait for more data
      // or may be an async operation, or body paused
      // request_in_progress should stay true
      // do not close, do not reset
      break;

    default:
      close_client(client);
      break;
    }
  }
}

// Resume reading after backpressure pause (called from body.c)
void resume_client_read(client_t *client) {
  if (!client || client->closing)
    return;
  
  if (uv_is_closing((uv_handle_t *)&client->handle))
    return;
  
  int result = uv_read_start((uv_stream_t *)&client->handle, 
                              server_alloc_buffer, 
                              server_on_read);
  if (result != 0) {
    LOG_ERROR("Failed to resume read: %s", uv_strerror(result));
  } else {
    LOG_DEBUG("Client read resumed successfully");
  }
}

static void on_connection(uv_stream_t *server, int status) {
  (void)server;

  if (status < 0) {
    LOG_ERROR("Connection error");
    return;
  }

  if (ecewo_server.shutdown_requested)
    return;

  if (ecewo_server.active_connections >= MAX_CONNECTIONS) {
    LOG_DEBUG("Max connections (%d) reached", MAX_CONNECTIONS);
    return;
  }

  client_t *client = calloc(1, sizeof(client_t));
  if (!client)
    return;

  client->valid = true;
  client->last_activity = uv_now(ecewo_server.loop);
  client->keep_alive_enabled = false;
  client->next = NULL;
  client->parser_initialized = false;
  client->request_in_progress = false;
  client->connection_arena = NULL;

  atomic_init(&client->refcount, 1);

  if (client_connection_init(client) != 0) {
    free(client);
    return;
  }

  if (uv_tcp_init(ecewo_server.loop, &client->handle) != 0) {
    if (client->connection_arena)
      arena_return(client->connection_arena);

    free(client);
    return;
  }

  client->handle.data = client;
  client->read_buf = uv_buf_init(client->buffer, READ_BUFFER_SIZE);

  if (uv_accept(server, (uv_stream_t *)&client->handle) == 0) {
    uv_tcp_nodelay(&client->handle, 1);

    if (uv_read_start((uv_stream_t *)&client->handle, 
                      server_alloc_buffer, 
                      server_on_read) == 0) {
      add_client_to_list(client);
      ecewo_server.active_connections++;
    } else {
      close_client(client);
    }
  } else {
    close_client(client);
  }
}

int server_listen(uint16_t port) {
  if (port == 0) {
    LOG_ERROR("Invalid port %" PRIu16 " (must be 1-65535)", port);
    return SERVER_INVALID_PORT;
  }

  if (!ecewo_server.initialized)
    return SERVER_NOT_INITIALIZED;

  if (ecewo_server.running)
    return SERVER_ALREADY_RUNNING;

  ecewo_server.server = malloc(sizeof(uv_tcp_t));
  if (!ecewo_server.server)
    return SERVER_OUT_OF_MEMORY;

  if (uv_tcp_init(ecewo_server.loop, ecewo_server.server) != 0) {
    free(ecewo_server.server);
    ecewo_server.server = NULL;
    return SERVER_INIT_FAILED;
  }

  uv_tcp_simultaneous_accepts(ecewo_server.server, 1);

  struct sockaddr_in addr;
  uv_ip4_addr("0.0.0.0", port, &addr);

  const char *is_test = getenv("ECEWO_TEST_MODE");
  unsigned int flags = 0;

#if !defined(_WIN32) && !defined(__APPLE__)
  if (!is_test || strcmp(is_test, "1") != 0)
    flags = UV_TCP_REUSEPORT;
#endif

  if (uv_tcp_bind(ecewo_server.server, (const struct sockaddr *)&addr, flags) != 0) {
    uv_close((uv_handle_t *)ecewo_server.server, on_server_closed);
    LOG_ERROR("Failed to bind to port %" PRIu16 " (may be in use)", port);
    return SERVER_BIND_FAILED;
  }

  if (uv_listen((uv_stream_t *)ecewo_server.server, LISTEN_BACKLOG, on_connection) != 0) {
    uv_close((uv_handle_t *)ecewo_server.server, on_server_closed);
    LOG_ERROR("Failed to listen on port %" PRIu16, port);
    return SERVER_LISTEN_FAILED;
  }

  if (start_cleanup_timer() != 0)
    LOG_DEBUG("Failed to start cleanup timer");

  ecewo_server.running = 1;

  const char *is_worker = getenv("ECEWO_WORKER");
  if (!is_worker || strcmp(is_worker, "1") != 0)
    printf("Server listening on http://localhost:%" PRIu16 "\n", port);

  return SERVER_OK;
}

void server_run(void) {
  if (!ecewo_server.initialized || !ecewo_server.running) {
    LOG_ERROR("Server not initialized or not listening");
    return;
  }

  uv_run(ecewo_server.loop, UV_RUN_DEFAULT);
  server_cleanup();
}

void server_atexit(shutdown_callback_t callback) {
  ecewo_server.shutdown_callback = callback;
}

bool server_is_running(void) {
  return ecewo_server.running;
}

int get_active_connections(void) {
  return ecewo_server.active_connections;
}

uv_loop_t *get_loop(void) {
  return ecewo_server.loop;
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

Timer *set_timeout(timer_callback_t callback, uint64_t delay_ms, void *user_data) {
  if (!ecewo_server.initialized || !callback)
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

  if (uv_timer_init(ecewo_server.loop, timer) != 0) {
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

Timer *set_interval(timer_callback_t callback, uint64_t interval_ms, void *user_data) {
  if (!ecewo_server.initialized || !callback)
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

  if (uv_timer_init(ecewo_server.loop, timer) != 0) {
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

void clear_timer(Timer *timer) {
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

bool client_is_valid(void *client_socket_data) {
  if (!client_socket_data)
    return false;
  
  client_t *client = (client_t *)client_socket_data;
  
  if (!client->valid || client->closing)
    return false;
  
  if (uv_is_closing((uv_handle_t *)&client->handle))
    return false;
  
  return true;
}
