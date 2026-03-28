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

static void global_tag_mw(Req *req, Res *res, Next next) {
  set_context(req, "global", "yes");
  next(req, res);
}

static void api_tag_mw(Req *req, Res *res, Next next) {
  set_context(req, "api", "yes");
  next(req, res);
}

static void tag_handler(Req *req, Res *res) {
  const char *global_tag = get_context(req, "global");
  const char *api_tag = get_context(req, "api");

  char *buf = arena_sprintf(req->arena, "global=%s,api=%s",
                            global_tag ? global_tag : "no",
                            api_tag ? api_tag : "no");
  send_text(res, 200, buf);
}

int test_global_use_runs_everywhere(void) {
  MockParams params = { .method = MOCK_GET, .path = "/use-public" };
  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("global=yes,api=no", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_path_use_runs_for_prefix(void) {
  MockParams params = { .method = MOCK_GET, .path = "/use-api/data" };
  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("global=yes,api=yes", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_path_use_runs_for_exact_match(void) {
  MockParams params = { .method = MOCK_GET, .path = "/use-api" };
  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("global=yes,api=yes", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_path_use_skipped_for_nonmatching(void) {
  MockParams params = { .method = MOCK_GET, .path = "/use-apiv2" };
  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("global=yes,api=no", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  use(app, global_tag_mw);
  use(app, "/use-api", api_tag_mw);

  get(app, "/use-public", tag_handler);
  get(app, "/use-api", tag_handler);
  get(app, "/use-api/data", tag_handler);
  get(app, "/use-apiv2", tag_handler);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_global_use_runs_everywhere);
  RUN_TEST(test_path_use_runs_for_prefix);
  RUN_TEST(test_path_use_runs_for_exact_match);
  RUN_TEST(test_path_use_skipped_for_nonmatching);

  mock_cleanup();
  return 0;
}
