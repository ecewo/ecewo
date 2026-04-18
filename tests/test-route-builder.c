// MIT License

// Copyright (c) 2026 Savas Sahin <savashn@proton.me>

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

// Tests for the ecewo_route_new / ecewo_route_middleware / ecewo_route_handler builder API.
// These are the FFI-friendly equivalents of the ECEWO_GET/POST/... macros.

#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"

// ---------------------------------------------------------------------------
// Handlers and middleware shared across tests
// ---------------------------------------------------------------------------

static void handler_hello(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "hello");
}

static void handler_created(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 201, "created");
}

static void handler_updated(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "updated");
}

static void handler_patched(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "patched");
}

static void handler_deleted(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "deleted");
}

static int mw_call_count = 0;

static void mw_a(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  mw_call_count++;
  ecewo_context_set(req, "mw_a", "1");
  next(req, res);
}

static void mw_b(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  mw_call_count++;
  ecewo_context_set(req, "mw_b", "1");
  next(req, res);
}

static void mw_block(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  (void)req;
  (void)next;
  ecewo_send_text(res, 403, "blocked");
}

static void handler_check_mw(ecewo_request_t *req, ecewo_response_t *res) {
  const char *a = ecewo_context_get(req, "mw_a");
  const char *b = ecewo_context_get(req, "mw_b");
  if (a && b)
    ecewo_send_text(res, 200, "both");
  else if (a)
    ecewo_send_text(res, 200, "a-only");
  else
    ecewo_send_text(res, 200, "none");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Builder with no middleware, handler only
int test_builder_no_middleware(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/builder/get"
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("hello", res.body);
  free_request(&res);
  RETURN_OK();
}

// Builder with one middleware
int test_builder_one_middleware(void) {
  mw_call_count = 0;
  MockParams params = {
    .method = MOCK_GET,
    .path = "/builder/one-mw"
  };
  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("a-only", res.body);
  ASSERT_EQ(1, mw_call_count);
  free_request(&res);
  RETURN_OK();
}

// Builder with two middleware in order
int test_builder_two_middleware(void) {
  mw_call_count = 0;
  MockParams params = {
    .method = MOCK_GET,
    .path = "/builder/two-mw"
  };
  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("both", res.body);
  ASSERT_EQ(2, mw_call_count);
  free_request(&res);
  RETURN_OK();
}

// Builder with blocking middleware, handler must not be reached
int test_builder_middleware_blocks(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/builder/block"
  };
  MockResponse res = request(&params);
  ASSERT_EQ(403, res.status_code);
  ASSERT_EQ_STR("blocked", res.body);
  free_request(&res);
  RETURN_OK();
}

// Builder for POST
int test_builder_post(void) {
  MockParams params = { .method = MOCK_POST, .path = "/builder/post" };
  MockResponse res = request(&params);
  ASSERT_EQ(201, res.status_code);
  ASSERT_EQ_STR("created", res.body);
  free_request(&res);
  RETURN_OK();
}

// Builder for PUT
int test_builder_put(void) {
  MockParams params = { .method = MOCK_PUT, .path = "/builder/put" };
  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("updated", res.body);
  free_request(&res);
  RETURN_OK();
}

// Builder for PATCH
int test_builder_patch(void) {
  MockParams params = { .method = MOCK_PATCH, .path = "/builder/patch" };
  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("patched", res.body);
  free_request(&res);
  RETURN_OK();
}

// Builder for DELETE
int test_builder_delete(void) {
  MockParams params = { .method = MOCK_DELETE, .path = "/builder/delete" };
  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("deleted", res.body);
  free_request(&res);
  RETURN_OK();
}

static void setup_routes(ecewo_app_t *app) {
  ecewo_route_t *r;

  r = ecewo_route_new(app, ECEWO_METHOD_GET, "/builder/get");
  ecewo_route_handler(r, handler_hello);

  r = ecewo_route_new(app, ECEWO_METHOD_GET, "/builder/one-mw");
  ecewo_route_middleware(r, mw_a);
  ecewo_route_handler(r, handler_check_mw);

  r = ecewo_route_new(app, ECEWO_METHOD_GET, "/builder/two-mw");
  ecewo_route_middleware(r, mw_a);
  ecewo_route_middleware(r, mw_b);
  ecewo_route_handler(r, handler_check_mw);

  r = ecewo_route_new(app, ECEWO_METHOD_GET, "/builder/block");
  ecewo_route_middleware(r, mw_block);
  ecewo_route_handler(r, handler_hello);

  r = ecewo_route_new(app, ECEWO_METHOD_POST, "/builder/post");
  ecewo_route_handler(r, handler_created);

  r = ecewo_route_new(app, ECEWO_METHOD_PUT, "/builder/put");
  ecewo_route_handler(r, handler_updated);

  r = ecewo_route_new(app, ECEWO_METHOD_PATCH, "/builder/patch");
  ecewo_route_handler(r, handler_patched);

  r = ecewo_route_new(app, ECEWO_METHOD_DELETE, "/builder/delete");
  ecewo_route_handler(r, handler_deleted);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_builder_no_middleware);
  RUN_TEST(test_builder_one_middleware);
  RUN_TEST(test_builder_two_middleware);
  RUN_TEST(test_builder_middleware_blocks);
  RUN_TEST(test_builder_post);
  RUN_TEST(test_builder_put);
  RUN_TEST(test_builder_patch);
  RUN_TEST(test_builder_delete);

  mock_cleanup();
  return 0;
}
