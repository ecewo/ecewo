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

void handler_body(Req *req, Res *res) {
  const char *body_str = req->body ? (const char *)req->body : "0";

  char *response = ecewo_sprintf(req->arena,
                                 "len=%zu, body=%s, method=%s",
                                 req->body_len,
                                 body_str,
                                 req->method);

  ecewo_send_text(res, 200, response);
}

int test_method_get(void) {
  MockParams params = { .method = MOCK_GET, .path = "/method" };
  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=0, body=0, method=GET", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_method_post(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/method",
    .body = "{\"test\":true}"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=13, body={\"test\":true}, method=POST", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_method_put(void) {
  MockParams params = {
    .method = MOCK_PUT,
    .path = "/method",
    .body = "{\"test\":true}"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=13, body={\"test\":true}, method=PUT", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_method_delete(void) {
  MockParams params = {
    .method = MOCK_DELETE,
    .path = "/method"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=0, body=0, method=DELETE", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_method_patch(void) {
  MockParams params = {
    .method = MOCK_PATCH,
    .path = "/method",
    .body = "{\"test\":true}"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=13, body={\"test\":true}, method=PATCH", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  ECEWO_GET(app, "/method", handler_body);
  ECEWO_POST(app, "/method", handler_body);
  ECEWO_PUT(app, "/method", handler_body);
  ECEWO_DELETE(app, "/method", handler_body);
  ECEWO_PATCH(app, "/method", handler_body);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_method_get);
  RUN_TEST(test_method_post);
  RUN_TEST(test_method_put);
  RUN_TEST(test_method_delete);
  RUN_TEST(test_method_patch);
  mock_cleanup();
  return 0;
}
