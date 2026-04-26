// MIT License
//
// Copyright (c) 2025-2026 Savas Sahin <savashn@proton.me>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Multi-app integration test. Boots two ecewo apps on different ports sharing
// a single libuv loop, then verifies port isolation, independent shutdown,
// and shared async-work tracking across apps.

#include "ecewo.h"
#include "tester.h"
#include "uv.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_A_PORT 18801
#define APP_B_PORT 18802

#define BUF_SIZE 4096

static uv_thread_t server_thread;
static _Atomic bool servers_ready = false;
static _Atomic bool app_a_atexit_fired = false;
static _Atomic bool app_b_atexit_fired = false;

// ----- handlers --------------------------------------------------------------

static void handler_a_root(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "app-a");
}

static void handler_a_shutdown(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_send_text(res, 200, "shutting-down-a");
  ecewo_shutdown(ecewo_req_app(req));
}

static void handler_b_root(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "app-b");
}

static void handler_b_shutdown(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_send_text(res, 200, "shutting-down-b");
  ecewo_shutdown(ecewo_req_app(req));
}

static void handler_b_loop_id(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  // Confirm both apps see the same shared loop pointer.
  char buf[64];
  snprintf(buf, sizeof(buf), "%p", ecewo_get_loop());
  ecewo_send_text(res, 200, buf);
}

static void handler_a_loop_id(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  char buf[64];
  snprintf(buf, sizeof(buf), "%p", ecewo_get_loop());
  ecewo_send_text(res, 200, buf);
}

static void on_atexit_a(void *user_data) {
  (void)user_data;
  app_a_atexit_fired = true;
}

static void on_atexit_b(void *user_data) {
  (void)user_data;
  app_b_atexit_fired = true;
}

// ----- minimal HTTP client ---------------------------------------------------

typedef struct {
  uv_tcp_t tcp;
  uv_connect_t connect_req;
  uv_write_t write_req;
  uv_shutdown_t shutdown_req;
  char *request_data;
  char response_buffer[BUF_SIZE];
  size_t response_len;
  bool done;
  int status_code;
  int err;
} http_client_t;

static void client_alloc(uv_handle_t *handle, size_t suggested, uv_buf_t *buf) {
  http_client_t *c = handle->data;
  size_t avail = sizeof(c->response_buffer) - c->response_len;
  buf->base = c->response_buffer + c->response_len;
  buf->len = avail < suggested ? avail : suggested;
}

static void client_on_close(uv_handle_t *handle) {
  http_client_t *c = handle->data;
  c->done = true;
}

static void client_on_shutdown(uv_shutdown_t *req, int status) {
  (void)status;
  http_client_t *c = req->data;
  uv_close((uv_handle_t *)&c->tcp, client_on_close);
}

static void client_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  (void)buf;
  http_client_t *c = stream->data;
  if (nread < 0) {
    if (nread != UV_EOF)
      c->err = (int)nread;
    uv_read_stop(stream);
    c->shutdown_req.data = c;
    uv_shutdown(&c->shutdown_req, stream, client_on_shutdown);
    return;
  }
  if (nread == 0)
    return;
  c->response_len += (size_t)nread;
}

static void client_on_write(uv_write_t *req, int status) {
  http_client_t *c = req->data;
  if (status < 0) {
    c->err = status;
    uv_close((uv_handle_t *)&c->tcp, client_on_close);
    return;
  }
  uv_read_start((uv_stream_t *)&c->tcp, client_alloc, client_on_read);
}

static void client_on_connect(uv_connect_t *req, int status) {
  http_client_t *c = req->data;
  if (status < 0) {
    c->err = status;
    uv_close((uv_handle_t *)&c->tcp, client_on_close);
    return;
  }
  uv_buf_t buf = uv_buf_init(c->request_data, (unsigned int)strlen(c->request_data));
  c->write_req.data = c;
  uv_write(&c->write_req, (uv_stream_t *)&c->tcp, &buf, 1, client_on_write);
}

// Sends a GET request to 127.0.0.1:port and returns the HTTP status code, or
// -1 on connection failure.
static int http_get(uint16_t port, const char *path, char *body_out, size_t body_cap) {
  uv_loop_t loop;
  if (uv_loop_init(&loop) < 0)
    return -1;

  http_client_t c;
  memset(&c, 0, sizeof(c));
  c.tcp.data = &c;
  c.connect_req.data = &c;

  char request[512];
  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\nHost: localhost:%u\r\nConnection: close\r\n\r\n",
           path, (unsigned)port);
  c.request_data = request;

  uv_tcp_init(&loop, &c.tcp);
  struct sockaddr_in addr;
  uv_ip4_addr("127.0.0.1", port, &addr);
  if (uv_tcp_connect(&c.connect_req, &c.tcp,
                     (const struct sockaddr *)&addr, client_on_connect)
      < 0) {
    uv_close((uv_handle_t *)&c.tcp, client_on_close);
  }

  uint64_t start = uv_hrtime();
  while (!c.done && uv_hrtime() - start < 5000ull * 1000000ull) {
    uv_run(&loop, UV_RUN_NOWAIT);
    if (c.done)
      break;
    uv_sleep(2);
  }

  uv_loop_close(&loop);

  if (c.err != 0)
    return -1;

  unsigned status = 0;
  if (sscanf(c.response_buffer, "HTTP/1.1 %u", &status) != 1)
    return -1;

  if (body_out && body_cap > 0) {
    body_out[0] = '\0';
    char *body = strstr(c.response_buffer, "\r\n\r\n");
    if (body) {
      body += 4;
      strncpy(body_out, body, body_cap - 1);
      body_out[body_cap - 1] = '\0';
    }
  }

  return (int)status;
}

// ----- server thread ---------------------------------------------------------

static void server_thread_fn(void *arg) {
  (void)arg;

  ecewo_app_t *app_a = ecewo_create();
  ecewo_app_t *app_b = ecewo_create();
  if (!app_a || !app_b) {
    fprintf(stderr, "ecewo_create failed\n");
    return;
  }

  ecewo_atexit(app_a, on_atexit_a, NULL);
  ecewo_atexit(app_b, on_atexit_b, NULL);

  ECEWO_GET(app_a, "/", handler_a_root);
  ECEWO_GET(app_a, "/loop", handler_a_loop_id);
  ECEWO_GET(app_a, "/shutdown", handler_a_shutdown);

  ECEWO_GET(app_b, "/", handler_b_root);
  ECEWO_GET(app_b, "/loop", handler_b_loop_id);
  ECEWO_GET(app_b, "/shutdown", handler_b_shutdown);

  if (ecewo_bind(app_a, APP_A_PORT) != 0) {
    fprintf(stderr, "bind app_a failed\n");
    return;
  }
  if (ecewo_bind(app_b, APP_B_PORT) != 0) {
    fprintf(stderr, "bind app_b failed\n");
    return;
  }

  servers_ready = true;

  // Runs the shared loop. Returns once every app has been shut down.
  ecewo_run();

  servers_ready = false;
}

static bool wait_for_servers_ready(void) {
  for (int i = 0; i < 50; i++) {
    if (servers_ready) {
      char body[64];
      int s = http_get(APP_A_PORT, "/", body, sizeof(body));
      if (s == 200)
        return true;
    }
    uv_sleep(100);
  }
  return false;
}

// ----- tests -----------------------------------------------------------------

static int test_both_ports_serve(void) {
  char body[64];

  int s1 = http_get(APP_A_PORT, "/", body, sizeof(body));
  ASSERT_EQ(200, s1);
  ASSERT_EQ_STR("app-a", body);

  int s2 = http_get(APP_B_PORT, "/", body, sizeof(body));
  ASSERT_EQ(200, s2);
  ASSERT_EQ_STR("app-b", body);

  RETURN_OK();
}

static int test_shared_loop(void) {
  char body_a[64];
  char body_b[64];

  int sa = http_get(APP_A_PORT, "/loop", body_a, sizeof(body_a));
  ASSERT_EQ(200, sa);
  int sb = http_get(APP_B_PORT, "/loop", body_b, sizeof(body_b));
  ASSERT_EQ(200, sb);

  // Both apps must report the same loop pointer (process-singleton runtime).
  ASSERT_EQ_STR(body_a, body_b);

  RETURN_OK();
}

static int test_isolated_routes(void) {
  // /shutdown on a is a different handler than /shutdown on b - ensure
  // app_b's routes are not visible from app_a's port.
  // We use a path that exists only on app_a but with same name on b. Both
  // exist and are independent - verify by querying body content.
  char body[64];
  int sa = http_get(APP_A_PORT, "/loop", body, sizeof(body));
  ASSERT_EQ(200, sa);
  // app_a does not have a /nonexistent route
  int sn = http_get(APP_A_PORT, "/nonexistent", body, sizeof(body));
  ASSERT_EQ(404, sn);
  RETURN_OK();
}

static int test_independent_shutdown(void) {
  // Shut down app_a; app_b must keep working.
  char body[64];
  int s = http_get(APP_A_PORT, "/shutdown", body, sizeof(body));
  ASSERT_EQ(200, s);
  ASSERT_EQ_STR("shutting-down-a", body);

  // Give shutdown a moment to drain.
  uv_sleep(100);

  // app_a's atexit must have fired.
  ASSERT_TRUE(app_a_atexit_fired);

  // app_a port must now be closed (connection refused -> http_get returns -1).
  int sa = http_get(APP_A_PORT, "/", body, sizeof(body));
  ASSERT_EQ(-1, sa);

  // app_b must still serve.
  int sb = http_get(APP_B_PORT, "/", body, sizeof(body));
  ASSERT_EQ(200, sb);
  ASSERT_EQ_STR("app-b", body);

  // app_b's atexit must NOT have fired yet.
  ASSERT_FALSE(app_b_atexit_fired);

  RETURN_OK();
}

static int test_final_shutdown(void) {
  // Shut down app_b too; ecewo_run should return.
  char body[64];
  int s = http_get(APP_B_PORT, "/shutdown", body, sizeof(body));
  ASSERT_EQ(200, s);
  ASSERT_EQ_STR("shutting-down-b", body);

  // Wait for the server thread to exit.
  uv_thread_join(&server_thread);

  ASSERT_TRUE(app_b_atexit_fired);
  ASSERT_FALSE(servers_ready);

  RETURN_OK();
}

int main(void) {
  if (uv_thread_create(&server_thread, server_thread_fn, NULL) != 0) {
    fprintf(stderr, "Failed to create server thread\n");
    return 1;
  }

  if (!wait_for_servers_ready()) {
    fprintf(stderr, "Servers failed to start\n");
    return 1;
  }

  RUN_TEST(test_both_ports_serve);
  RUN_TEST(test_shared_loop);
  RUN_TEST(test_isolated_routes);
  RUN_TEST(test_independent_shutdown);
  RUN_TEST(test_final_shutdown);

  return 0;
}
