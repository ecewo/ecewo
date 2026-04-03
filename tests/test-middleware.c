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

static int middleware_order_tracker = 0;

void middleware_first(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  int *order = ecewo_alloc(req->arena, sizeof(int));
  *order = ++middleware_order_tracker;
  ecewo_context_set(req, "first", order);
  next(req, res);
}

void middleware_second(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  int *order = ecewo_alloc(req->arena, sizeof(int));
  *order = ++middleware_order_tracker;
  ecewo_context_set(req, "second", order);
  next(req, res);
}

void middleware_third(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  int *order = ecewo_alloc(req->arena, sizeof(int));
  *order = ++middleware_order_tracker;
  ecewo_context_set(req, "third", order);
  next(req, res);
}

void handler_middleware_order(ecewo_request_t *req, ecewo_response_t *res) {
  int *first = ecewo_context_get(req, "first");
  int *second = ecewo_context_get(req, "second");
  int *third = ecewo_context_get(req, "third");

  char *response = ecewo_sprintf(req->arena, "%d,%d,%d",
                                 first ? *first : 0,
                                 second ? *second : 0,
                                 third ? *third : 0);

  ecewo_send_text(res, 200, response);
}

void middleware_abort(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  (void)req;
  (void)next;
  ecewo_send_text(res, 403, "Forbidden by middleware");
  return; // Don't call next
}

void handler_should_not_reach(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "Should not see this");
}

int test_middleware_execution_order(void) {
  middleware_order_tracker = 0;

  MockParams params = {
    .method = MOCK_GET,
    .path = "/mw-order"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("1,2,3", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_middleware_abort(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/mw-abort"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(403, res.status_code);
  ASSERT_EQ_STR("Forbidden by middleware", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/mw-order", middleware_first, middleware_second, middleware_third, handler_middleware_order);
  ECEWO_GET(app, "/mw-abort", middleware_abort, handler_should_not_reach);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_middleware_execution_order);
  RUN_TEST(test_middleware_abort);

  mock_cleanup();
  return 0;
}