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

void handler_single_param(ecewo_request_t *req, ecewo_response_t *res) {
  const char *id = ecewo_param(req, "userId");
  if (!id) {
    ecewo_send_text(res, 400, "Missing id");
    return;
  }
  char *response = ecewo_sprintf(req->arena, "id=%s", id);
  ecewo_send_text(res, 200, response);
}

void handler_multi_param(ecewo_request_t *req, ecewo_response_t *res) {
  const char *userId = ecewo_param(req, "userId");
  const char *postId = ecewo_param(req, "postId");
  const char *commentId = ecewo_param(req, "commentId");

  if (!userId || !postId || !commentId) {
    ecewo_send_text(res, 400, "Missing params");
    return;
  }

  char *response = ecewo_sprintf(req->arena, "%s/%s/%s", userId, postId, commentId);
  ecewo_send_text(res, 200, response);
}

void handler_overflow_param(ecewo_request_t *req, ecewo_response_t *res) {
  const char *id1 = ecewo_param(req, "id1");
  const char *id2 = ecewo_param(req, "id2");
  const char *id3 = ecewo_param(req, "id3");
  const char *id4 = ecewo_param(req, "id4");
  const char *id5 = ecewo_param(req, "id5");
  const char *id6 = ecewo_param(req, "id6");
  const char *id7 = ecewo_param(req, "id7");
  const char *id8 = ecewo_param(req, "id8");
  const char *id9 = ecewo_param(req, "id9");
  const char *id10 = ecewo_param(req, "id10");

  char *response = ecewo_sprintf(req->arena, "%s/%s/%s/%s/%s/%s/%s/%s/%s/%s",
                                 id1, id2, id3, id4, id5, id6, id7, id8, id9, id10);

  ecewo_send_text(res, 200, response);
}

int test_single_param(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/users/42"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_NOT_NULL(res.body);
  ASSERT_EQ_STR("id=42", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_multi_param(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/users/100/posts/200/comments/300"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_NOT_NULL(res.body);
  ASSERT_EQ_STR("100/200/300", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_param_special_chars(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/users/test-user-123"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_NOT_NULL(res.body);
  ASSERT_EQ_STR("id=test-user-123", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_overflow_param(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/param/1/2/3/4/5/6/7/8/9/10"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_NOT_NULL(res.body);
  ASSERT_EQ_STR("1/2/3/4/5/6/7/8/9/10", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/param/:id1/:id2/:id3/:id4/:id5/:id6/:id7/:id8/:id9/:id10", handler_overflow_param);
  ECEWO_GET(app, "/users/:userId/posts/:postId/comments/:commentId", handler_multi_param);
  ECEWO_GET(app, "/users/:userId", handler_single_param);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_single_param);
  RUN_TEST(test_multi_param);
  RUN_TEST(test_param_special_chars);
  RUN_TEST(test_overflow_param);
  mock_cleanup();
  return 0;
}
