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

typedef struct {
  int chunks_received;
  size_t total_bytes;
  bool body_null_in_handler;
  bool body_null_during_chunk;
} StreamContext;


void chunk_callback(Req *req, const uint8_t *data, size_t len) {
  StreamContext *ctx = ecewo_get_context(req, "stream_ctx");
  ctx->chunks_received++;
  ctx->total_bytes += len;

  if (ctx->chunks_received == 1)
    ctx->body_null_during_chunk = (req->body == NULL);
}

void end_callback(Req *req, Res *res) {
  StreamContext *ctx = ecewo_get_context(req, "stream_ctx");
  char *response = arena_sprintf(req->arena,
    "chunks=%d,bytes=%zu,handler_null=%d,chunk_null=%d",
    ctx->chunks_received,
    ctx->total_bytes,
    ctx->body_null_in_handler ? 1 : 0,
    ctx->body_null_during_chunk ? 1 : 0
  );
  ecewo_send_text(res, OK, response);
}


void handler_streaming_test(Req *req, Res *res) {
  StreamContext *ctx = arena_alloc(req->arena, sizeof(StreamContext));
  memset(ctx, 0, sizeof(StreamContext));

  ctx->body_null_in_handler = (req->body == NULL);

  ecewo_set_context(req, "stream_ctx", ctx);
  ecewo_body_on_data(req, chunk_callback);
  ecewo_body_on_end(req, res, end_callback);
}

int test_streaming_mode(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/streaming",
    .body = "Test body content"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_TRUE(strstr(res.body, "chunks=1") != NULL);
  ASSERT_TRUE(strstr(res.body, "bytes=17") != NULL);
  ASSERT_TRUE(strstr(res.body, "handler_null=1") != NULL);
  ASSERT_TRUE(strstr(res.body, "chunk_null=1") != NULL);

  free_request(&res);
  RETURN_OK();
}


void handler_buffered(Req *req, Res *res) {
  char *response = arena_sprintf(req->arena, "len=%zu,body='%s'",
    req->body_len, req->body ? (const char *)req->body : "NULL");
  ecewo_send_text(res, OK, response);
}

int test_buffered_mode(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/buffered",
    .body = "Buffered test"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_TRUE(strstr(res.body, "len=13") != NULL);
  ASSERT_TRUE(strstr(res.body, "Buffered test") != NULL);

  free_request(&res);
  RETURN_OK();
}


void handler_size_limit(Req *req, Res *res) {
  ecewo_body_limit(req, 10);
  ecewo_body_on_data(req, chunk_callback);
  ecewo_body_on_end(req, res, end_callback);
}

int test_size_limit(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/size-limit",
    .body = "This body is definitely longer than 10 bytes"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(413, res.status_code);

  free_request(&res);
  RETURN_OK();
}


static void setup_routes(App *app) {
  ECEWO_POST(app, "/streaming", ecewo_body_stream, handler_streaming_test);
  ECEWO_POST(app, "/buffered", handler_buffered);
  ECEWO_POST(app, "/size-limit", ecewo_body_stream, handler_size_limit);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_streaming_mode);
  RUN_TEST(test_buffered_mode);
  RUN_TEST(test_size_limit);

  mock_cleanup();
  return 0;
}
