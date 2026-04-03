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

// -------------------------------------------------------------------------
// Handlers
// -------------------------------------------------------------------------

// Echoes the :name dynamic param
static void param_handler(ecewo_request_t *req, ecewo_response_t *res) {
  const char *name = ecewo_param(req, "name");
  ecewo_send_text(res, 200, name ? name : "no-param");
}

// Echoes the "q" query value
static void search_handler(ecewo_request_t *req, ecewo_response_t *res) {
  const char *q = ecewo_query(req, "q");
  ecewo_send_text(res, 200, q ? q : "no-query");
}

// Echoes both a dynamic param and a query value
static void param_and_query_handler(ecewo_request_t *req, ecewo_response_t *res) {
  const char *name = ecewo_param(req, "name");
  const char *q = ecewo_query(req, "q");
  char *response = ecewo_sprintf(req->arena, "name=%s,q=%s",
                                 name ? name : "null",
                                 q ? q : "null");
  ecewo_send_text(res, 200, response);
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/users/:name", param_handler);
  ECEWO_GET(app, "/search", search_handler);
  ECEWO_GET(app, "/users/:name/search", param_and_query_handler);
}

// -------------------------------------------------------------------------
// Dynamic param decoding
// -------------------------------------------------------------------------

static int test_param_space_decoded(void) {
  // %20 in a dynamic param should decode to a space
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/users/john%20doe",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("john doe", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_param_cyrillic_decoded(void) {
  // %D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82 = "привет" (UTF-8)
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/users/%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("привет", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_param_plus_is_literal(void) {
  // '+' in a path param is NOT decoded to a space (only %xx decoding,
  // like decodeURIComponent). Use %20 for a space in path params.
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/users/john+doe",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("john+doe", res.body);
  free_request(&res);
  RETURN_OK();
}

// -------------------------------------------------------------------------
// Query string decoding
// -------------------------------------------------------------------------

static int test_query_space_decoded(void) {
  // %20 in a query value should decode to a space
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/search?q=hello%20world",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("hello world", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_query_cyrillic_decoded(void) {
  // %D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82 = "привет" (UTF-8)
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/search?q=%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("привет", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_query_plus_decoded(void) {
  // '+' in a query value should decode to a space
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/search?q=hello+world",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("hello world", res.body);
  free_request(&res);
  RETURN_OK();
}

// -------------------------------------------------------------------------
// Combined: encoded param + encoded query
// -------------------------------------------------------------------------

static int test_param_and_query_both_decoded(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/users/john%20doe/search?q=%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("name=john doe,q=привет", res.body);
  free_request(&res);
  RETURN_OK();
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_param_space_decoded);
  RUN_TEST(test_param_cyrillic_decoded);
  RUN_TEST(test_param_plus_is_literal);

  RUN_TEST(test_query_space_decoded);
  RUN_TEST(test_query_cyrillic_decoded);
  RUN_TEST(test_query_plus_decoded);

  RUN_TEST(test_param_and_query_both_decoded);

  mock_cleanup();
  return 0;
}
