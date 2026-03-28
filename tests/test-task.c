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
  Res *res;
  int result;
} compute_ctx_t;

static void compute_work(void *context) {
  compute_ctx_t *ctx = (compute_ctx_t *)context;
  ctx->result = 42 * 2;
}

static void compute_done(void *context) {
  compute_ctx_t *ctx = (compute_ctx_t *)context;

  char *response = arena_sprintf(ctx->res->arena, "result=%d", ctx->result);
  send_text(ctx->res, 200, response);
}

void handler_compute(Req *req, Res *res) {
  compute_ctx_t *ctx = arena_alloc(req->arena, sizeof(compute_ctx_t));
  ctx->res = res;
  ctx->result = 0;

  spawn(ctx, compute_work, compute_done);
}

int test_spawn_with_response(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/compute"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_NOT_NULL(res.body);
  ASSERT_EQ_STR("result=84", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  get(app, "/compute", handler_compute);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_spawn_with_response);
  mock_cleanup();
  return 0;
}
