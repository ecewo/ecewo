// MIT License

// Copyright (c) 2025-2026 Savas Sahin <savashn@proton.me>

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "tester.h"
#include "ecewo.h"
#include "ecewo-mock.h"
#include "uv.h"
#include <stdlib.h>
#include <inttypes.h>

static uint64_t get_time_ms(void) {
  return uv_hrtime() / 1000000;
}

static uint64_t get_thread_id(void) {
  return (uint64_t)(uintptr_t)uv_thread_self();
}

// ============================================================================
// Spawn Thread ID Test
// ============================================================================

typedef struct {
  Res *res;
  uint64_t main_thread_id;
  uint64_t work_thread_id;
  uint64_t done_thread_id;
} thread_test_ctx_t;

static void thread_test_work(void *context) {
  thread_test_ctx_t *ctx = context;
  ctx->work_thread_id = get_thread_id();
  uv_sleep(100);
}

static void thread_test_done(void *context) {
  thread_test_ctx_t *ctx = context;
  ctx->done_thread_id = get_thread_id();

  char *response = arena_sprintf(ctx->res->arena, "%" PRIu64 ",%" PRIu64 ",%" PRIu64,
                                 ctx->main_thread_id,
                                 ctx->work_thread_id,
                                 ctx->done_thread_id);

  ecewo_send_text(ctx->res, 200, response);
}

// ============================================================================
// HANDLERS
// ============================================================================

void handler_thread_test(Req *req, Res *res) {
  (void)req;
  thread_test_ctx_t *ctx = arena_alloc(res->arena, sizeof(thread_test_ctx_t));
  ctx->res = res;
  ctx->main_thread_id = get_thread_id();
  ctx->work_thread_id = 0;
  ctx->done_thread_id = 0;

  ecewo_spawn(ctx, thread_test_work, thread_test_done);
}

void handler_get_main_thread(Req *req, Res *res) {
  (void)req;
  char *response = arena_sprintf(res->arena, "%" PRIu64, get_thread_id());
  ecewo_send_text(res, 200, response);
}

void handler_fast(Req *req, Res *res) {
  (void)req;
  ecewo_send_text(res, 200, "fast");
}

void handler_slow(Req *req, Res *res) {
  (void)req;
  uv_sleep(300);
  ecewo_send_text(res, 200, "slow");
}

// ============================================================================
// Background Request Helper
// ============================================================================

typedef struct {
  const char *path;
  MockResponse response;
  uint64_t duration_ms;
} async_ctx_t;

static void background_request(void *arg) {
  async_ctx_t *ctx = (async_ctx_t *)arg;
  uint64_t start = get_time_ms();

  MockParams params = {
    .method = MOCK_GET,
    .path = ctx->path
  };

  ctx->response = request(&params);
  ctx->duration_ms = get_time_ms() - start;
}

// ============================================================================
// Tests
// ============================================================================

int test_spawn_thread_ids(void) {
  MockParams main_params = {
    .method = MOCK_GET,
    .path = "/main-thread"
  };

  MockResponse main_res = request(&main_params);
  ASSERT_EQ(200, main_res.status_code);

  uint64_t server_main_thread;
  sscanf(main_res.body, "%" SCNu64, &server_main_thread);

  // printf("\n  Server main thread: %" PRIu64 "\n", server_main_thread);
  free_request(&main_res);

  MockParams spawn_params = {
    .method = MOCK_GET,
    .path = "/thread-test"
  };

  MockResponse spawn_res = request(&spawn_params);
  ASSERT_EQ(200, spawn_res.status_code);
  ASSERT_NOT_NULL(spawn_res.body);

  uint64_t handler_tid, work_tid, done_tid;

  int parsed = sscanf(spawn_res.body, "%" SCNu64 ",%" SCNu64 ",%" SCNu64,
                      &handler_tid,
                      &work_tid,
                      &done_tid);

  ASSERT_EQ(3, parsed);

  // printf("  Handler thread: %" PRIu64 "\n", handler_tid);
  // printf("  Work thread:    %" PRIu64 "\n", work_tid);
  // printf("  Done thread:    %" PRIu64 "\n", done_tid);

  ASSERT_EQ(server_main_thread, handler_tid);
  ASSERT_NE(server_main_thread, work_tid);
  ASSERT_EQ(server_main_thread, done_tid);

  free_request(&spawn_res);
  RETURN_OK();
}

int test_spawn_not_blocking(void) {
  async_ctx_t slow_ctx = {
    .path = "/thread-test"
  };

  uv_thread_t thread;
  uv_thread_create(&thread, background_request, &slow_ctx);

  uv_sleep(30);
  uint64_t fast_start = get_time_ms();

  MockParams fast_params = {
    .method = MOCK_GET,
    .path = "/fast"
  };

  MockResponse fast_res = request(&fast_params);
  uint64_t fast_duration = get_time_ms() - fast_start;

  uv_thread_join(&thread);

  ASSERT_EQ(200, slow_ctx.response.status_code);
  ASSERT_EQ(200, fast_res.status_code);
  ASSERT_TRUE(fast_duration < 50);

  free_request(&slow_ctx.response);
  free_request(&fast_res);
  RETURN_OK();
}

int test_sync_blocking(void) {
  async_ctx_t slow_ctx = {
    .path = "/slow"
  };

  uv_thread_t thread;
  uv_thread_create(&thread, background_request, &slow_ctx);

  uv_sleep(30);
  uint64_t fast_start = get_time_ms();

  MockParams fast_params = {
    .method = MOCK_GET,
    .path = "/fast"
  };

  MockResponse fast_res = request(&fast_params);
  uint64_t fast_duration = get_time_ms() - fast_start;

  uv_thread_join(&thread);

  ASSERT_EQ(200, slow_ctx.response.status_code);
  ASSERT_EQ(200, fast_res.status_code);
  ASSERT_TRUE(fast_duration >= 200);

  free_request(&slow_ctx.response);
  free_request(&fast_res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  ECEWO_GET(app, "/thread-test", handler_thread_test);
  ECEWO_GET(app, "/main-thread", handler_get_main_thread);
  ECEWO_GET(app, "/fast", handler_fast);
  ECEWO_GET(app, "/slow", handler_slow);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_spawn_thread_ids);
  RUN_TEST(test_spawn_not_blocking);
  RUN_TEST(test_sync_blocking);
  mock_cleanup();
  return 0;
}
