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

#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"
#include <string.h>

static void ok_handler(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "ok");
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/users", ok_handler);
  ECEWO_POST(app, "/users", ok_handler);
  ECEWO_GET(app, "/users/:id", ok_handler);
  ECEWO_GET(app, "/files/*", ok_handler);
}

// POST /users is registered -> GET and POST are both allowed
static int test_wrong_method_returns_405(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_DELETE,
    .path = "/users"
  });
  ASSERT_EQ(405, res.status_code);
  const char *allow = mock_get_header(&res, "Allow");
  ASSERT_NOT_NULL(allow);
  free_request(&res);
  RETURN_OK();
}

// Allow header must list all registered methods for the path
static int test_allow_header_lists_registered_methods(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_PUT,
    .path = "/users"
  });
  ASSERT_EQ(405, res.status_code);
  const char *allow = mock_get_header(&res, "Allow");
  ASSERT_NOT_NULL(allow);
  // Both GET and POST are registered for /users
  ASSERT_TRUE(strstr(allow, "GET") != NULL);
  ASSERT_TRUE(strstr(allow, "POST") != NULL);
  free_request(&res);
  RETURN_OK();
}

// Completely unknown path must still return 404, not 405
static int test_unknown_path_returns_404(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/no-such-route"
  });
  ASSERT_EQ(404, res.status_code);
  free_request(&res);
  RETURN_OK();
}

// Param route: DELETE /users/42 -> 405 (only GET is registered)
static int test_param_route_wrong_method_returns_405(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_DELETE,
    .path = "/users/42"
  });
  ASSERT_EQ(405, res.status_code);
  const char *allow = mock_get_header(&res, "Allow");
  ASSERT_NOT_NULL(allow);
  ASSERT_TRUE(strstr(allow, "GET") != NULL);
  free_request(&res);
  RETURN_OK();
}

// Wildcard route: POST /files/a/b/c -> 405 (only GET is registered via wildcard)
static int test_wildcard_route_wrong_method_returns_405(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_POST,
    .path = "/files/a/b/c"
  });
  ASSERT_EQ(405, res.status_code);
  const char *allow = mock_get_header(&res, "Allow");
  ASSERT_NOT_NULL(allow);
  ASSERT_TRUE(strstr(allow, "GET") != NULL);
  free_request(&res);
  RETURN_OK();
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_wrong_method_returns_405);
  RUN_TEST(test_allow_header_lists_registered_methods);
  RUN_TEST(test_unknown_path_returns_404);
  RUN_TEST(test_param_route_wrong_method_returns_405);
  RUN_TEST(test_wildcard_route_wrong_method_returns_405);

  mock_cleanup();
  return 0;
}
