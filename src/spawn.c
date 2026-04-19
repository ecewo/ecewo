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

#include "uv.h"
#include "ecewo.h"
#include "logger.h"
#include "server.h"
#include <stdlib.h>

// ============= SPAWN (Background work) =============

typedef struct {
  uv_work_t work;
  uv_async_t async_send;
  void *context;
  ecewo_spawn_handler_t work_fn;
  ecewo_spawn_handler_t result_fn;
} spawn_t;

static void spawn_cleanup_cb(uv_handle_t *handle) {
  spawn_t *t = (spawn_t *)handle->data;
  if (t)
    free(t);
}

static void spawn_async_cb(uv_async_t *handle) {
  spawn_t *t = (spawn_t *)handle->data;
  if (!t)
    return;

  if (t->result_fn)
    t->result_fn(t->context);

  uv_close((uv_handle_t *)handle, spawn_cleanup_cb);
}

static void spawn_work_cb(uv_work_t *req) {
  spawn_t *t = (spawn_t *)req->data;
  if (t && t->work_fn)
    t->work_fn(t->context);
}

static void spawn_after_work_cb(uv_work_t *req, int status) {
  spawn_t *t = (spawn_t *)req->data;
  if (!t)
    return;

  if (status < 0)
    LOG_ERROR("Spawn execution failed");

  uv_async_send(&t->async_send);
}

int ecewo_spawn(void *context, ecewo_spawn_handler_t work_fn, ecewo_spawn_handler_t done_fn) {
  if (!work_fn)
    return -1;

  // Freed in spawn_cleanup_cb after the uv_async handle closes.
  spawn_t *task = calloc(1, sizeof(spawn_t));
  if (!task)
    return -1;

  if (uv_async_init(((uv_loop_t *)ecewo_get_loop()), &task->async_send, spawn_async_cb) != 0) {
    free(task);
    return -1;
  }

  task->work.data = task;
  task->async_send.data = task;
  task->context = context;
  task->work_fn = work_fn;
  task->result_fn = done_fn;

  int result = uv_queue_work(
      ((uv_loop_t *)ecewo_get_loop()),
      &task->work,
      spawn_work_cb,
      spawn_after_work_cb);

  if (result != 0) {
    uv_close((uv_handle_t *)&task->async_send, NULL);
    free(task);
    return result;
  }

  return 0;
}

// ============= SPAWN_HTTP (With response) =============

typedef struct {
  uv_work_t work;
  uv_async_t async_send;
  void *context;
  ecewo_spawn_handler_t work_fn;
  ecewo_spawn_done_t done_fn;
  ecewo_response_t *res;
  ecewo_client_t *client;
} spawn_http_t;

static void spawn_http_cleanup_cb(uv_handle_t *handle) {
  spawn_http_t *t = (spawn_http_t *)handle->data;

  if (t) {
    if (t->client)
      ecewo_client_unref(t->client);
    free(t);
  }
}

static void spawn_http_async_cb(uv_async_t *handle) {
  spawn_http_t *t = (spawn_http_t *)handle->data;

  if (!t)
    return;

  ecewo_response_t *res = t->res;
  ecewo_client_t *client = t->client;

  if (!client) {
    uv_close((uv_handle_t *)handle, spawn_http_cleanup_cb);
    return;
  }

  if (!client->valid || client->closing) {
    uv_close((uv_handle_t *)handle, spawn_http_cleanup_cb);
    return;
  }

  if (uv_is_closing((uv_handle_t *)&client->handle)) {
    uv_close((uv_handle_t *)handle, spawn_http_cleanup_cb);
    return;
  }

  if (t->done_fn)
    t->done_fn(res, t->context);

  uv_close((uv_handle_t *)handle, spawn_http_cleanup_cb);
}

static void spawn_http_work_cb(uv_work_t *req) {
  spawn_http_t *t = (spawn_http_t *)req->data;
  if (t && t->work_fn)
    t->work_fn(t->context);
}

static void spawn_http_after_work_cb(uv_work_t *req, int status) {
  spawn_http_t *t = (spawn_http_t *)req->data;
  if (!t)
    return;

  if (status < 0)
    LOG_ERROR("Async spawn execution failed");

  uv_async_send(&t->async_send);
}

int ecewo_spawn_http(ecewo_response_t *res, void *context, ecewo_spawn_handler_t work_fn, ecewo_spawn_done_t done_fn) {

  if (!res || !work_fn)
    return -1;

  if (!res->ecewo__client_socket || !((uv_tcp_t *)res->ecewo__client_socket)->data)
    return -1;

  // Freed in spawn_http_cleanup_cb after the uv_async handle closes.
  spawn_http_t *task = calloc(1, sizeof(spawn_http_t));
  if (!task)
    return -1;

  if (uv_async_init(((uv_loop_t *)ecewo_get_loop()), &task->async_send, spawn_http_async_cb) != 0) {
    free(task);
    return -1;
  }

  task->work.data = task;
  task->async_send.data = task;
  task->context = context;
  task->work_fn = work_fn;
  task->done_fn = done_fn;

  task->res = res;
  task->client = NULL;
  if (res && res->ecewo__client_socket && ((uv_tcp_t *)res->ecewo__client_socket)->data) {
    task->client = (ecewo_client_t *)((uv_tcp_t *)res->ecewo__client_socket)->data;
    ecewo_client_ref(task->client);
  }

  int result = uv_queue_work(
      ((uv_loop_t *)ecewo_get_loop()),
      &task->work,
      spawn_http_work_cb,
      spawn_http_after_work_cb);

  if (result != 0) {
    uv_close((uv_handle_t *)&task->async_send, NULL);
    if (task->client)
      ecewo_client_unref(task->client);
    free(task);
    return result;
  }

  return 0;
}
