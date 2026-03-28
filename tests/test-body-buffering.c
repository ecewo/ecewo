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

void handler_body(Req *req, Res *res) {
  char *response = arena_sprintf(req->arena, "received=%zu", req->body_len);
  send_text(res, 200, response);
}

int test_large_body(void) {
  // 1MB body
  size_t size = 1024 * 1024;
  char *large_body = malloc(size + 1);
  memset(large_body, 'A', size);
  large_body[size] = '\0';

  MockParams params = {
    .method = MOCK_POST,
    .path = "/large-body",
    .body = large_body
  };

  MockResponse res = request(&params);
  ASSERT_EQ(413, res.status_code);
  ASSERT_EQ_STR("Payload Too Large", res.body);

  free(large_body);
  free_request(&res);
  RETURN_OK();
}

int test_normal_body(void) {
  size_t size = 1024 * 1024 - 1;
  char *normal_body = malloc(size + 1);
  memset(normal_body, 'A', size);
  normal_body[size] = '\0';

  MockParams params = {
    .method = MOCK_POST,
    .path = "/normal-body",
    .body = normal_body
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("received=1048575", res.body);

  free(normal_body);
  free_request(&res);
  RETURN_OK();
}

static void setup_routes(App *app) {
  post(app, "/large-body", handler_body);
  post(app, "/normal-body", handler_body);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_large_body);
  RUN_TEST(test_normal_body);

  mock_cleanup();
  return 0;
}
