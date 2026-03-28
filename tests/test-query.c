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

void handler_query_params(Req *req, Res *res) {
  const char *page = ecewo_get_query(req, "page");
  const char *limit = ecewo_get_query(req, "limit");
  const char *sort = ecewo_get_query(req, "sort");

  char *response = arena_sprintf(req->arena, "page=%s,limit=%s,sort=%s",
                                 page ? page : "null",
                                 limit ? limit : "null",
                                 sort ? sort : "null");

  ecewo_send_text(res, 200, response);
}

int test_query_multiple(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/search?page=1&limit=10&sort=desc"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("page=1,limit=10,sort=desc", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_query_empty_value(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/search?page=1&limit=&sort=asc"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  // Empty value should result in null (based on parse_query logic)
  ASSERT_EQ_STR("page=1,limit=null,sort=asc", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_query_no_params(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/search"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("page=null,limit=null,sort=null", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  ECEWO_GET(app, "/search", handler_query_params);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_query_multiple);
  RUN_TEST(test_query_empty_value);
  RUN_TEST(test_query_no_params);

  mock_cleanup();
  return 0;
}
