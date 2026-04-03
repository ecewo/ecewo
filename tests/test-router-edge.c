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

// Route "/:": one segment, is_param=true, param name has length 0.
// ecewo_param(req, "") retrieves the captured value.
static void bare_colon_handler(ecewo_request_t *req, ecewo_response_t *res) {
  const char *val = ecewo_param(req, "");
  ecewo_send_text(res, 200, val ? val : "no-param");
}

// Route "/prefix/*/suffix"
// The "suffix" literal segment in the pattern is unreachable because
// match_dynamic_entry returns true as soon as it encounters the '*', consuming
// all remaining request segments without examining the rest of the pattern.
static void wildcard_mid_handler(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "wildcard-mid");
}

// Route "/encoded%2Fpath": a static route whose URL contains the three-byte
// sequence "%2F". The routing layer never decodes percent-escaped characters,
// so this only matches requests where those three bytes appear literally.
static void encoded_handler(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, 200, "encoded");
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/:", bare_colon_handler);
  ECEWO_GET(app, "/prefix/*/suffix", wildcard_mid_handler);
  ECEWO_GET(app, "/encoded%2Fpath", encoded_handler);
}

// -------------------------------------------------------------------------
// Edge case 1: bare ":" produces a parameter with an empty name
// -------------------------------------------------------------------------

static int test_bare_colon_captures_segment(void) {
  // Request to "/hello": the single segment "hello" is captured under the
  // empty param name and echoed by the handler.
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/hello"
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("hello", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_bare_colon_captures_different_segment(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/world"
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("world", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_bare_colon_no_match_for_extra_segments(void) {
  // "/hello/extra" is two segments; "/:" is one segment. No match.
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/hello/extra"
  });
  ASSERT_EQ(404, res.status_code);
  free_request(&res);
  RETURN_OK();
}

// -------------------------------------------------------------------------
// Edge case 2; wildcard not at the end of a pattern
//
// Pattern "/prefix/*/suffix": the '*' is at segment index 1.
// match_dynamic_entry returns true immediately upon seeing the wildcard,
// so the "suffix" literal at index 2 is NEVER checked.
//
// Consequence: "/prefix/*/suffix" behaves identically to "/prefix/*"
//; it matches any request that starts with at least the first "prefix"
// segment, regardless of what follows.
//
// Note: "/prefix" alone (1 segment) matches "/:" first because "/:' has
// seg_count=1 and is registered before "/prefix/*/suffix" (seg_count=3).
// First-registered wins at the same depth; the wildcard's "zero or more"
// semantics are shadowed here by "/:" taking priority.
//
// If you need to match the trailing literal, use "/prefix/:param/suffix"
// instead.
// -------------------------------------------------------------------------

static int test_wildcard_mid_matches_single_segment_after_prefix(void) {
  // "/prefix/anything"; two segments; wildcard at pos 1 fires immediately
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/prefix/anything"
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("wildcard-mid", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_wildcard_mid_matches_multiple_segments_after_prefix(void) {
  // "/prefix/a/b/c"; four segments; wildcard still fires at pos 1
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/prefix/a/b/c"
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("wildcard-mid", res.body);
  free_request(&res);
  RETURN_OK();
}

// "/prefix"; one segment; is claimed by "/:" (seg_count=1, registered first)
// not by "/prefix/*/suffix" (seg_count=3). Verify the expected winner.
static int test_bare_colon_beats_wildcard_for_prefix_alone(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/prefix"
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("prefix", res.body); // echoed by bare_colon_handler
  free_request(&res);
  RETURN_OK();
}

static int test_wildcard_mid_does_not_match_different_prefix(void) {
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path = "/other/anything"
  });
  ASSERT_EQ(404, res.status_code);
  free_request(&res);
  RETURN_OK();
}

// -------------------------------------------------------------------------
// Edge case 3: percent-encoded characters are opaque to the router
//
// The routing layer never decodes percent-escaped sequences. "%2F" is the
// three bytes '%', '2', 'F'; not a path separator. A registered route
// "/encoded%2Fpath" only matches a request URL that contains those exact
// bytes; the URL "/encoded/path" (with a real slash) does NOT match.
// -------------------------------------------------------------------------

static int test_percent_encoded_matches_literal_bytes(void) {
  // "/encoded%2Fpath" matches the route "/encoded%2Fpath" exactly.
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path   = "/encoded%2Fpath",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("encoded", res.body);
  free_request(&res);
  RETURN_OK();
}

static int test_real_slash_does_not_match_encoded_route(void) {
  // "/encoded/path" splits into two segments; the route "/encoded%2Fpath"
  // is a single-segment static route; they are different keys in rax.
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path   = "/encoded/path",
  });
  ASSERT_EQ(404, res.status_code);
  free_request(&res);
  RETURN_OK();
}

static int test_other_percent_sequences_are_literal(void) {
  // "/encoded%20path" is a single segment (routing is on raw bytes, so
  // "%20" is not a separator). Caught by "/:"; the param value is decoded
  // after extraction, so ecewo_param() returns "encoded path".
  MockResponse res = request(&(MockParams){
    .method = MOCK_GET,
    .path   = "/encoded%20path",
  });
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("encoded path", res.body);
  free_request(&res);
  RETURN_OK();
}

// -------------------------------------------------------------------------
// main
// -------------------------------------------------------------------------

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_bare_colon_captures_segment);
  RUN_TEST(test_bare_colon_captures_different_segment);
  RUN_TEST(test_bare_colon_no_match_for_extra_segments);

  RUN_TEST(test_wildcard_mid_matches_single_segment_after_prefix);
  RUN_TEST(test_wildcard_mid_matches_multiple_segments_after_prefix);
  RUN_TEST(test_bare_colon_beats_wildcard_for_prefix_alone);
  RUN_TEST(test_wildcard_mid_does_not_match_different_prefix);

  RUN_TEST(test_percent_encoded_matches_literal_bytes);
  RUN_TEST(test_real_slash_does_not_match_encoded_route);
  RUN_TEST(test_other_percent_sequences_are_literal);

  mock_cleanup();
  return 0;
}
