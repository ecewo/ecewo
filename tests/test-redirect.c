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

void handler_redirect(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_redirect(res, MOVED_PERMANENTLY, "/new-location");
}

void handler_new_location(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, OK, "New page content");
}

int test_redirect(void) {
  MockParams params1 = {
    .method = MOCK_GET,
    .path = "/old-path"
  };

  MockResponse res1 = request(&params1);

  ASSERT_EQ(301, res1.status_code);

  const char *location = mock_get_header(&res1, "Location");
  ASSERT_NOT_NULL(location);
  ASSERT_EQ_STR("/new-location", location);

  MockParams params2 = {
    .method = MOCK_GET,
    .path = location
  };

  MockResponse res2 = request(&params2);

  ASSERT_EQ(200, res2.status_code);
  ASSERT_EQ_STR("New page content", res2.body);

  free_request(&res1);
  free_request(&res2);
  RETURN_OK();
}

void handler_redirect_injection(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;

  const char *evil_url = "https://motherfuckingmaliciouswebsite.com\r\nSet-Cookie: session=stolen";
  ecewo_redirect(res, 302, evil_url);
}

int test_redirect_injection(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/ecewo_redirect-injection"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(400, res.status_code);
  ASSERT_EQ_STR("Bad Request", res.body);

  ASSERT_NULL(mock_get_header(&res, "Location"));
  ASSERT_NULL(mock_get_header(&res, "Set-Cookie"));

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/old-path", handler_redirect);
  ECEWO_GET(app, "/new-location", handler_new_location);
  ECEWO_GET(app, "/ecewo_redirect-injection", handler_redirect_injection);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_redirect);
  RUN_TEST(test_redirect_injection);
  mock_cleanup();
  return 0;
}
