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
#include "uv.h"

static int background_counter = 0;

typedef struct {
  Arena *arena;
  int increment;
} background_ctx_t;

static void background_work(void *context) {
  background_ctx_t *ctx = context;
  background_counter += ctx->increment;
  ecewo_arena_return(ctx->arena);
}

void handler_fire_and_forget(Req *req, Res *res) {
  (void)req;
  Arena *bg_arena = ecewo_arena_borrow();

  background_ctx_t *ctx = ecewo_alloc(bg_arena, sizeof(background_ctx_t));
  ctx->arena = bg_arena;
  ctx->increment = 10;

  ecewo_spawn(ctx, background_work, NULL);
  ecewo_send_text(res, ACCEPTED, "Status: Accepted");
}

void handler_check_counter(Req *req, Res *res) {
  (void)req;
  char *response = ecewo_sprintf(req->arena, "Counter: %d", background_counter);
  ecewo_send_text(res, 200, response);
}

int test_spawn_fire_and_forget(void) {
  background_counter = 0;

  MockParams params1 = {
    .method = MOCK_POST,
    .path = "/background"
  };

  MockResponse res1 = request(&params1);
  ASSERT_EQ(202, res1.status_code);
  ASSERT_EQ_STR("Status: Accepted", res1.body);
  free_request(&res1);

  uv_sleep(100);

  MockParams params2 = {
    .method = MOCK_GET,
    .path = "/check-counter"
  };

  MockResponse res2 = request(&params2);
  ASSERT_EQ(200, res2.status_code);
  ASSERT_EQ_STR("Counter: 10", res2.body);
  free_request(&res2);

  RETURN_OK();
}

static void setup_routes(App *app) {
  ECEWO_POST(app, "/background", handler_fire_and_forget);
  ECEWO_GET(app, "/check-counter", handler_check_counter);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_spawn_fire_and_forget);
  mock_cleanup();
  return 0;
}
