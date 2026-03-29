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
#include <string.h>

typedef struct
{
  char *user_id;
  char *role;
} user_ctx_t;

// ============================================================================
// TEST 1: Basic Context (Happy Path)
// ============================================================================

void context_middleware(Req *req, Res *res, Next next) {
  const char *token = ecewo_get_header(req, "Authorization");

  if (!token) {
    ecewo_send_text(res, 401, "Unauthorized");
    return;
  }

  user_ctx_t *ctx = ecewo_alloc(req->arena, sizeof(user_ctx_t));
  ctx->user_id = ecewo_strdup(req->arena, "user123");
  ctx->role = ecewo_strdup(req->arena, "admin");

  ecewo_set_context(req, "user_ctx", ctx);

  next(req, res);
}

void context_handler(Req *req, Res *res) {
  user_ctx_t *ctx = (user_ctx_t *)ecewo_get_context(req, "user_ctx");

  if (!ctx) {
    ecewo_send_text(res, 500, "Internal Server Error");
    return;
  }

  if (strcmp(ctx->user_id, "user123") != 0 || strcmp(ctx->role, "admin") != 0) {
    ecewo_send_text(res, FORBIDDEN, "Forbidden");
    return;
  }

  ecewo_send_text(res, 200, "Success!");
}

int test_context_basic(void) {
  MockHeaders headers[] = {
    { "Authorization", "Bearer token" }
  };

  MockParams params = {
    .method = MOCK_GET,
    .path = "/context",
    .headers = headers,
    .header_count = 1
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("Success!", res.body);

  free_request(&res);
  RETURN_OK();
}

// ============================================================================
// TEST 2: Missing Context (No Middleware)
// ============================================================================

void handler_no_middleware(Req *req, Res *res) {
  user_ctx_t *ctx = (user_ctx_t *)ecewo_get_context(req, "user_ctx");

  if (!ctx) {
    ecewo_send_text(res, 500, "No context");
    return;
  }

  ecewo_send_text(res, 200, "OK");
}

int test_context_missing(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/no-middleware"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(500, res.status_code);
  ASSERT_EQ_STR("No context", res.body);

  free_request(&res);
  RETURN_OK();
}

// ============================================================================
// TEST 3: Non-Existent Key
// ============================================================================

void handler_nonexistent_key(Req *req, Res *res) {
  void *value = ecewo_get_context(req, "nonexistent_key");

  ecewo_send_text(res, 200, value ? "found" : "null");
}

int test_context_nonexistent_key(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/nonexistent-key"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("null", res.body);

  free_request(&res);
  RETURN_OK();
}

// ============================================================================
// TEST 4: Context Overwrite
// ============================================================================

void handler_overwrite(Req *req, Res *res) {
  ecewo_set_context(req, "test", "value1");
  ecewo_set_context(req, "test", "value2"); // Should overwrite

  const char *value = ecewo_get_context(req, "test");

  ecewo_send_text(res, 200, value ? value : "null");
}

int test_context_overwrite(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/overwrite"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("value2", res.body);

  free_request(&res);
  RETURN_OK();
}

// ============================================================================
// TEST 5: Multiple Keys
// ============================================================================

void handler_multiple_keys(Req *req, Res *res) {
  ecewo_set_context(req, "key1", "a");
  ecewo_set_context(req, "key2", "b");
  ecewo_set_context(req, "key3", "c");

  const char *v1 = ecewo_get_context(req, "key1");
  const char *v2 = ecewo_get_context(req, "key2");
  const char *v3 = ecewo_get_context(req, "key3");

  char *response = ecewo_sprintf(req->arena, "%s,%s,%s", v1, v2, v3);
  ecewo_send_text(res, 200, response);
}

int test_context_multiple_keys(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/multiple-keys"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("a,b,c", res.body);

  free_request(&res);
  RETURN_OK();
}

// ============================================================================
// TEST 6: NULL Data
// ============================================================================

void handler_null_data(Req *req, Res *res) {
  ecewo_set_context(req, "test", NULL);
  void *value = ecewo_get_context(req, "test");
  ecewo_send_text(res, 200, value == NULL ? "null" : "not-null");
}

int test_context_null_data(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/null-data"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("null", res.body);

  free_request(&res);
  RETURN_OK();
}

// ============================================================================
// TEST 7: Middleware Chain Context Sharing
// ============================================================================

void middleware_first_ctx(Req *req, Res *res, Next next) {
  ecewo_set_context(req, "mw1", "first");
  next(req, res);
}

void middleware_second_ctx(Req *req, Res *res, Next next) {
  ecewo_set_context(req, "mw2", "second");
  next(req, res);
}

void handler_chain_context(Req *req, Res *res) {
  const char *v1 = ecewo_get_context(req, "mw1");
  const char *v2 = ecewo_get_context(req, "mw2");

  char *response = ecewo_sprintf(req->arena, "%s,%s",
                                 v1 ? v1 : "null",
                                 v2 ? v2 : "null");

  ecewo_send_text(res, 200, response);
}

int test_context_middleware_chain(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/chain-context"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("first,second", res.body);

  free_request(&res);
  RETURN_OK();
}

// ============================================================================
// TEST 8: Complex Data Structure
// ============================================================================

typedef struct
{
  int id;
  char *name;
  char *email;
  bool is_admin;
} complex_user_t;

void handler_complex_data(Req *req, Res *res) {
  complex_user_t *user = ecewo_alloc(req->arena, sizeof(complex_user_t));
  user->id = 123;
  user->name = ecewo_strdup(req->arena, "John Doe");
  user->email = ecewo_strdup(req->arena, "john@example.com");
  user->is_admin = true;

  ecewo_set_context(req, "user", user);

  // Retrieve and verify
  complex_user_t *retrieved = ecewo_get_context(req, "user");

  char *response = ecewo_sprintf(req->arena,
                                 "id:%d,name:%s,email:%s,admin:%s",
                                 retrieved->id,
                                 retrieved->name,
                                 retrieved->email,
                                 retrieved->is_admin ? "yes" : "no");

  ecewo_send_text(res, 200, response);
}

int test_context_complex_data(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/complex-data"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("id:123,name:John Doe,email:john@example.com,admin:yes", res.body);

  free_request(&res);
  RETURN_OK();
}

// ============================================================================
// TEST 9: Unauthorized (No Token)
// ============================================================================

int test_context_unauthorized(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/context"
    // No Authorization header
  };

  MockResponse res = request(&params);

  ASSERT_EQ(401, res.status_code);
  ASSERT_EQ_STR("Unauthorized", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  ECEWO_GET(app, "/context", context_middleware, context_handler);
  ECEWO_GET(app, "/no-middleware", handler_no_middleware);
  ECEWO_GET(app, "/nonexistent-key", handler_nonexistent_key);
  ECEWO_GET(app, "/overwrite", handler_overwrite);
  ECEWO_GET(app, "/multiple-keys", handler_multiple_keys);
  ECEWO_GET(app, "/null-data", handler_null_data);
  ECEWO_GET(app, "/chain-context", middleware_first_ctx, middleware_second_ctx, handler_chain_context);
  ECEWO_GET(app, "/complex-data", handler_complex_data);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_context_basic);
  RUN_TEST(test_context_missing);
  RUN_TEST(test_context_nonexistent_key);
  RUN_TEST(test_context_overwrite);
  RUN_TEST(test_context_multiple_keys);
  RUN_TEST(test_context_null_data);
  RUN_TEST(test_context_middleware_chain);
  RUN_TEST(test_context_unauthorized);

  mock_cleanup();
  return 0;
}
