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

void handler_echo_headers(Req *req, Res *res) {
  const char *auth = ecewo_get_header(req, "Authorization");
  const char *content_type = ecewo_get_header(req, "Content-Type");
  const char *custom = ecewo_get_header(req, "X-Custom-Header");

  char *response = ecewo_sprintf(req->arena, "auth=%s,ct=%s,custom=%s",
                                 auth ? auth : "null",
                                 content_type ? content_type : "null",
                                 custom ? custom : "null");

  ecewo_send_text(res, 200, response);
}

int test_request_headers(void) {
  MockHeaders headers[] = {
    { "Authorization", "Bearer token123" },
    { "Content-Type", "application/json" },
    { "X-Custom-Header", "custom-value" }
  };

  MockParams params = {
    .method = MOCK_GET,
    .path = "/headers",
    .headers = headers,
    .header_count = 3
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("auth=Bearer token123,ct=application/json,custom=custom-value", res.body);

  free_request(&res);
  RETURN_OK();
}

void handler_set_headers(Req *req, Res *res) {
  (void)req;
  ecewo_set_header(res, "X-Custom-Header", "test-value");
  ecewo_set_header(res, "X-Request-Id", "12345");
  ecewo_set_header(res, "Cache-Control", "no-cache");
  ecewo_send_text(res, 200, "OK");
}

int test_set_headers(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/custom-headers"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("OK", res.body);

  ASSERT_NOT_NULL(mock_get_header(&res, "Content-Type"));
  ASSERT_NOT_NULL(mock_get_header(&res, "content-type"));
  ASSERT_NOT_NULL(mock_get_header(&res, "CONTENT-TYPE"));
  ASSERT_NOT_NULL(mock_get_header(&res, "CoNtEnT-TyPe"));

  ASSERT_EQ_STR("text/plain", mock_get_header(&res, "Content-Type"));
  ASSERT_EQ_STR("test-value", mock_get_header(&res, "X-Custom-Header"));
  ASSERT_EQ_STR("12345", mock_get_header(&res, "X-Request-Id"));
  ASSERT_EQ_STR("no-cache", mock_get_header(&res, "Cache-Control"));

  free_request(&res);
  RETURN_OK();
}

void handler_header_injection(Req *req, Res *res) {
  (void)req;

  ecewo_set_header(res, "X-Evil", "value\r\nSet-Cookie: hacked=1");
  ecewo_set_header(res, "X-Valid", "normal-value");
  ecewo_send_text(res, 200, "OK");
}

int test_header_injection(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/header-injection"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);

  ASSERT_NULL(mock_get_header(&res, "X-Evil"));
  ASSERT_NULL(mock_get_header(&res, "Set-Cookie"));

  ASSERT_EQ_STR("normal-value", mock_get_header(&res, "X-Valid"));

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  ECEWO_GET(app, "/headers", handler_echo_headers);
  ECEWO_GET(app, "/custom-headers", handler_set_headers);
  ECEWO_GET(app, "/header-injection", handler_header_injection);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_request_headers);
  RUN_TEST(test_set_headers);
  RUN_TEST(test_header_injection);
  mock_cleanup();
  return 0;
}
