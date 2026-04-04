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

typedef struct {
  ecewo_request_t *req;
  ecewo_next_t next;
} mw_ctx_t;

typedef struct {
  char *user_id;
  char *role;
} user_ctx_t;

static void auth_work(void *context) {
  (void)context;
  uv_sleep(100);
}

static void auth_done(ecewo_response_t *res, void *context) {
  mw_ctx_t *ctx = context;

  user_ctx_t *user = ecewo_alloc(ecewo_res_arena(res), sizeof(user_ctx_t));
  user->user_id = ecewo_strdup(ecewo_res_arena(res), "user123");
  user->role = ecewo_strdup(ecewo_res_arena(res), "admin");

  ecewo_context_set(ctx->req, "user", user);

  ctx->next(ctx->req, res);
}

void middleware_async_auth(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  const char *token = ecewo_header_get(req, "Authorization");

  if (!token) {
    ecewo_send_text(res, 401, "Unauthorized");
    return;
  }

  mw_ctx_t *ctx = ecewo_alloc(ecewo_req_arena(req), sizeof(mw_ctx_t));
  ctx->req = req;
  ctx->next = next;

  ecewo_spawn_http(res, ctx, auth_work, auth_done);
}

void handler_protected(ecewo_request_t *req, ecewo_response_t *res) {
  user_ctx_t *user = ecewo_context_get(req, "user");

  if (!user) {
    ecewo_send_text(res, 500, "Internal Server Error");
    return;
  }

  char *response = ecewo_sprintf(ecewo_req_arena(req),
                                 "Welcome %s (role: %s)",
                                 user->user_id,
                                 user->role);

  ecewo_send_text(res, 200, response);
}

int test_async_auth_middleware(void) {
  MockHeaders headers[] = {
    { "Authorization", "Bearer token123" }
  };

  MockParams params = {
    .method = MOCK_GET,
    .path = "/mw-async",
    .headers = headers,
    .header_count = 1
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("Welcome user123 (role: admin)", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_async_auth_no_token(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/mw-async"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(401, res.status_code);
  ASSERT_EQ_STR("Unauthorized", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/mw-async", middleware_async_auth, handler_protected);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_async_auth_middleware);
  RUN_TEST(test_async_auth_no_token);

  mock_cleanup();
  return 0;
}
