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


// Stress test: route priority correctness with many same-depth patterns.
//
// Registers 10 specific GET routes (/a/:id … /j/:id) together with one
// generic catch-all (/:category/:id) and one static shortcut (/a/admin).
//
// Priority rules verified here:
//
//  1. Static routes always beat dynamic ones; the rax exact-match is tried
//     before any dynamic scan, regardless of registration order.
//
//  2. Within same-depth dynamic routes, the first-registered pattern wins.
//     The specific routes (/a/:id … /j/:id) are registered before the generic
//     (/:category/:id), so requests matching a known prefix hit the specific
//     handler, not the generic one.
//
//  3. The generic route acts as a fallback for prefixes with no specific
//     handler registered.

#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"
#include <stdio.h>
#include <string.h>

// Handlers return distinguishable status codes 
// so we can verify which route was matched without parsing the body.

static void specific_handler(ecewo_request_t *req, ecewo_response_t *res) {
  // Echo the captured :id value so the test can also verify param extraction
  const char *id = ecewo_get_param(req, "id");
  ecewo_send_text(res, 200, id ? id : "no-id");
}

static void generic_handler(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 201, "generic");
}

static void static_handler(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 202, "static");
}

static void setup_routes(ecewo_app_t *app) {
  // 10 specific same-depth routes registered FIRST
  ECEWO_GET(app, "/a/:id", specific_handler);
  ECEWO_GET(app, "/b/:id", specific_handler);
  ECEWO_GET(app, "/c/:id", specific_handler);
  ECEWO_GET(app, "/d/:id", specific_handler);
  ECEWO_GET(app, "/e/:id", specific_handler);
  ECEWO_GET(app, "/f/:id", specific_handler);
  ECEWO_GET(app, "/g/:id", specific_handler);
  ECEWO_GET(app, "/h/:id", specific_handler);
  ECEWO_GET(app, "/i/:id", specific_handler);
  ECEWO_GET(app, "/j/:id", specific_handler);

  // Static route registered after the dynamic ones; must still win (rule 1)
  ECEWO_GET(app, "/a/admin", static_handler);

  // Generic catch-all registered LAST; loses to all specific ones (rule 2)
  ECEWO_GET(app, "/:category/:id", generic_handler);
}

// --- test helpers --------------------------------------------------------

static int test_static_beats_dynamic(void) {
  // /a/admin is a static route.  Even though /a/:id and /:category/:id
  // were registered before it, the rax exact-match always runs first.
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/a/admin"
  });
  ASSERT_EQ(202, res.status_code);
  ASSERT_EQ_STR("static", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_all_specific_routes_reachable(void) {
  // Every specific route must be reachable and must capture the :id param
  static const char *prefixes[] = {"a","b","c","d","e","f","g","h","i","j"};
  for (int i = 0; i < 10; i++) {
    char path[32];
    snprintf(path, sizeof(path), "/%s/42", prefixes[i]);
    MockResponse res = request(&(MockParams){
      .method = MOCK_GET,
      .path = path
    });
    ASSERT_EQ(200, res.status_code);
    ASSERT_EQ_STR("42", res.body);
    free_request(&res);
  }
  RETURN_OK();
}

static int test_specific_beats_generic(void) {
  // /a/:id was registered before /:category/:id, so it must win
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/a/anything"
  });
  ASSERT_EQ(200, res.status_code); // specific, not generic (201)
  free_request(&res);
  RETURN_OK();
}

static int test_generic_fallback(void) {
  // A prefix not covered by any specific route falls through to /:category/:id
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/unknown/999"
  });
  ASSERT_EQ(201, res.status_code);
  ASSERT_EQ_STR("generic", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_no_cross_method_match(void) {
  // Routes are per-method; GET routes must not match POST requests
  // The path exists, GET is registered, so the response is 405, not 404
  MockResponse res = request(&(MockParams){
    .method = MOCK_POST,
    .path = "/a/123"
  });
  ASSERT_EQ(405, res.status_code);
  free_request(&res);
  RETURN_OK();
}

static int test_each_specific_gets_correct_id(void) {
  // Each route must capture only its own request's :id, not a neighbour's
  static const char *prefixes[] = {"b","c","d","e","f","g","h","i","j"};
  for (int i = 0; i < 9; i++) {
    char path[64];
    // Use a unique id per prefix
    snprintf(path, sizeof(path), "/%s/id-%d", prefixes[i], i + 100);
    MockResponse res = request(&(MockParams){
      .method = MOCK_GET,
      .path = path
    });
    ASSERT_EQ(200, res.status_code);
    char expected[32];
    snprintf(expected, sizeof(expected), "id-%d", i + 100);
    ASSERT_EQ_STR(expected, res.body);
    free_request(&res);
  }
  RETURN_OK();
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_static_beats_dynamic);
  RUN_TEST(test_all_specific_routes_reachable);
  RUN_TEST(test_specific_beats_generic);
  RUN_TEST(test_generic_fallback);
  RUN_TEST(test_no_cross_method_match);
  RUN_TEST(test_each_specific_gets_correct_id);

  mock_cleanup();
  return 0;
}
