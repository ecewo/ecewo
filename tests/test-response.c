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

// JSON
void handler_json_response(Req *req, Res *res) {
  (void)req;
  ecewo_send_json(res, 200, "{\"status\":\"ok\"}");
}

int test_json_content_type(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/json-response"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("{\"status\":\"ok\"}", res.body);

  free_request(&res);
  RETURN_OK();
}

// HTML
void handler_html_response(Req *req, Res *res) {
  (void)req;
  ecewo_send_html(res, 200, "<h1>Hello</h1>");
}

int test_html_content_type(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/html-response"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("<h1>Hello</h1>", res.body);

  free_request(&res);
  RETURN_OK();
}

// STATUS CODES
void handler_status_codes(Req *req, Res *res) {
  const char *code = ecewo_get_query(req, "code");
  if (!code) {
    ecewo_send_text(res, 400, "Missing code");
    return;
  }

  int status = atoi(code);
  ecewo_send_text(res, status, "Status test");
}

int test_status_201(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/status?code=201"
  };

  MockResponse res = request(&params);
  ASSERT_EQ(201, res.status_code);

  free_request(&res);
  RETURN_OK();
}

int test_status_404(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/status?code=404"
  };

  MockResponse res = request(&params);
  ASSERT_EQ(404, res.status_code);

  free_request(&res);
  RETURN_OK();
}

int test_status_500(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/status?code=500"
  };

  MockResponse res = request(&params);
  ASSERT_EQ(500, res.status_code);

  free_request(&res);
  RETURN_OK();
}

int test_404_unknown_path(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/this/path/does/not/exist"
  };

  MockResponse res = request(&params);
  ASSERT_EQ(404, res.status_code);

  free_request(&res);
  RETURN_OK();
}

int test_404_wrong_method(void) {
  // /users/:id regsitered as GET only
  MockParams params = {
    .method = MOCK_DELETE,
    .path = "/users/123"
  };

  MockResponse res = request(&params);
  ASSERT_EQ(404, res.status_code);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  ECEWO_GET(app, "/json-response", handler_json_response);
  ECEWO_GET(app, "/html-response", handler_html_response);
  ECEWO_GET(app, "/status", handler_status_codes);
}

int main(void) {
  mock_init(setup_routes);

  // Response Types
  RUN_TEST(test_json_content_type);
  RUN_TEST(test_html_content_type);
  RUN_TEST(test_status_201);
  RUN_TEST(test_status_404);
  RUN_TEST(test_status_500);
  RUN_TEST(test_404_unknown_path);
  RUN_TEST(test_404_wrong_method);

  mock_cleanup();
  return 0;
}
