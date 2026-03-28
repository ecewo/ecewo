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

void handler(Req *req, Res *res) {
  send_text(res, OK, req->path);
}

static int root_test(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("/", res.body);
  free_request(&res);

  RETURN_OK();
}

static int double_slashes_test(void) {
  // Request path "//" is normalized to root "/"
  // So it should match the "/" route
  MockParams params = {
    .method = MOCK_GET,
    .path = "//",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("//", res.body);
  free_request(&res);

  RETURN_OK();
}

static int param_test(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/users/123",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("/users/123", res.body);
  free_request(&res);

  RETURN_OK();
}

static int double_slash_param_test(void) {
  // Request "//users//123" tokenizes to ["users", "123"]
  // So it matches "/users/:id" route
  MockParams params = {
    .method = MOCK_GET,
    .path = "//users//123",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("//users//123", res.body);
  free_request(&res);

  RETURN_OK();
}

static int last_segment_test(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/users/123/",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("/users/123/", res.body);
  free_request(&res);

  RETURN_OK();
}

static int wildcard_test(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/files/anything/here",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("/files/anything/here", res.body);
  free_request(&res);

  RETURN_OK();
}

static void setup_routes(App *app) {
  get(app, "/", handler);
  get(app, "/users/:id", handler);
  get(app, "/files/*", handler);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(root_test);
  RUN_TEST(double_slashes_test);
  RUN_TEST(param_test);
  RUN_TEST(double_slash_param_test);
  RUN_TEST(last_segment_test);
  RUN_TEST(wildcard_test);

  mock_cleanup();
  return 0;
}