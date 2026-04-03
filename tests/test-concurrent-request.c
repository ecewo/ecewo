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

void handler_counter(ecewo_request_t *req, ecewo_response_t *res) {
  static int counter = 0;
  counter++;
  char *response = ecewo_sprintf(req->arena, "%d", counter);
  ecewo_send_text(res, 200, response);
}

int test_sequential_requests(void) {
  // 10 sequential request
  for (int i = 1; i <= 10; i++) {
    MockParams params = {
      .method = MOCK_GET,
      .path = "/counter"
    };

    MockResponse res = request(&params);
    ASSERT_EQ(200, res.status_code);

    int count = atoi(res.body);
    ASSERT_EQ(i, count);

    free_request(&res);
  }

  RETURN_OK();
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/counter", handler_counter);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_sequential_requests);
  mock_cleanup();
  return 0;
}
