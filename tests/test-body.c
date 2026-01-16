#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"

void handler_large_body(Req *req, Res *res) {
  char *response = arena_sprintf(req->arena, "received=%zu", req->body_len);
  send_text(res, 200, response);
}

int test_large_body(void) {
  // 10KB body
  size_t size = 10 * 1024;
  char *large_body = malloc(size + 1);
  memset(large_body, 'A', size);
  large_body[size] = '\0';

  MockParams params = {
    .method = MOCK_POST,
    .path = "/large-body",
    .body = large_body
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);

  char expected[32];
  snprintf(expected, sizeof(expected), "received=%zu", size);
  ASSERT_EQ_STR(expected, res.body);

  free(large_body);
  free_request(&res);
  RETURN_OK();
}

// STREAMING
static int chunks_received = 0;
static size_t total_bytes = 0;

// TODO: Fix the streaming test

bool on_chunk(Req *req, const char *data, size_t len, void *ctx) {
  (void)req;
  (void)ctx;
  
  chunks_received++;
  total_bytes += len;
  
  char *log = arena_sprintf(req->arena, "Chunk %d: %zu bytes", chunks_received, len);
  set_context(req, "last_chunk", log);
  
  return true; // Continue receiving
}

void on_complete(Req *req, void *ctx) {
  (void)ctx;
  Res *res = get_context(req, "_res");
  
  char *response = arena_sprintf(req->arena, 
    "Received %d chunks, total %zu bytes",
    chunks_received, total_bytes);

  printf("%s\n", response);
  
  send_text(res, OK, response);
}

void handler_streaming(Req *req, Res *res) {
  chunks_received = 0;
  total_bytes = 0;
  
  set_context(req, "_res", res);
  body_on_data(req, on_chunk, NULL);
  body_on_end(req, on_complete, NULL);
}

void handler_buffered(Req *req, Res *res) {
  // Body already available
  const char *body = body_bytes(req);
  size_t len = body_len(req);
  
  if (!body) {
    send_text(res, INTERNAL_SERVER_ERROR, "Body is NULL");
    return;
  }
  
  char *response = arena_sprintf(req->arena, 
    "Buffered: %zu bytes, body='%s'", len, body);
  
  send_text(res, OK, response);
}

int test_streaming_mode(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/streaming",
    .body = "Hello from streaming test!"
  };
  
  MockResponse res = request(&params);
  
  ASSERT_EQ(200, res.status_code);
  // Should have received 1 chunk (mock sends all at once)
  ASSERT_EQ_STR("Received 1 chunks, total 26 bytes", res.body);
  
  free_request(&res);
  RETURN_OK();
}

int test_buffered_mode(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/buffered",
    .body = "Hello from buffered test!"
  };
  
  MockResponse res = request(&params);
  
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("Buffered: 25 bytes, body='Hello from buffered test!'", res.body);
  
  free_request(&res);
  RETURN_OK();
}

int test_streaming_vs_buffered_isolation(void) {
  // Streaming mode should not affect buffered mode
  test_streaming_mode();
  test_buffered_mode();
  
  RETURN_OK();
}

static void setup_routes(void) {
  post("/large-body", handler_large_body);
  post("/streaming", handler_streaming);
  post("/buffered", handler_buffered);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_large_body);
  RUN_TEST(test_streaming_mode);
  RUN_TEST(test_buffered_mode);
  RUN_TEST(test_streaming_vs_buffered_isolation);

  mock_cleanup();
  return 0;
}
