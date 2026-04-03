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

#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"

typedef struct {
  ecewo_response_t *res;
  int total;
  int completed;
  int results[3];
  bool has_error;
} parallel_ctx_t;

static void parallel_work_1(void *context) {
  parallel_ctx_t *ctx = (parallel_ctx_t *)context;
  ctx->results[0] = 10;
}

static void parallel_work_2(void *context) {
  parallel_ctx_t *ctx = (parallel_ctx_t *)context;
  ctx->results[1] = 20;
}

static void parallel_work_3(void *context) {
  parallel_ctx_t *ctx = (parallel_ctx_t *)context;
  ctx->results[2] = 30;
}

static void parallel_done(void *context) {
  parallel_ctx_t *ctx = (parallel_ctx_t *)context;

  ctx->completed++;

  if (ctx->has_error && ctx->completed == 1) {
    ecewo_send_text(ctx->res, 500, "ecewo_spawn failed");
    return;
  }

  if (ctx->completed == ctx->total && !ctx->has_error) {
    int sum = ctx->results[0] + ctx->results[1] + ctx->results[2];
    char *response = ecewo_sprintf(ctx->res->arena, "{\"sum\":%d}", sum);
    ecewo_send_json(ctx->res, 200, response);
  }
}

void handler_parallel(ecewo_request_t *req, ecewo_response_t *res) {
  parallel_ctx_t *ctx = ecewo_alloc(req->arena, sizeof(parallel_ctx_t));
  ctx->res = res;
  ctx->total = 3;
  ctx->completed = 0;
  ctx->results[0] = 0;
  ctx->results[1] = 0;
  ctx->results[2] = 0;
  ctx->has_error = false;

  ecewo_spawn(ctx, parallel_work_1, parallel_done);
  ecewo_spawn(ctx, parallel_work_2, parallel_done);
  ecewo_spawn(ctx, parallel_work_3, parallel_done);
}

int test_spawn_parallel(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/parallel"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("{\"sum\":60}", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/parallel", handler_parallel);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_spawn_parallel);
  mock_cleanup();
  return 0;
}
