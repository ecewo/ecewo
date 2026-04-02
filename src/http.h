// Copyright 2025-2026 Savas Sahin <savashn@proton.me>

// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef ECEWO_HTTP_H
#define ECEWO_HTTP_H

#include "ecewo.h"
#include "request.h"
#include "llhttp.h"

#ifndef BUFFERED_BODY_MAX_SIZE
#define BUFFERED_BODY_MAX_SIZE (1UL * 1024UL * 1024UL) /* 1MB */
#endif

typedef enum {
  PARSE_SUCCESS = 0, // Fully parsed
  PARSE_INCOMPLETE = 1, // Need more data
  PARSE_PAUSED = 2, // Paused at headers-complete
  PARSE_ERROR = -1, // Parse error occurred
  PARSE_OVERFLOW = -2 // Size limit exceeded
} parse_result_t;

typedef enum {
  BODY_CHUNK_CONTINUE = 0,
  BODY_CHUNK_ERROR = -1
} body_chunk_result_t;

// Called by on_body_cb when a chunk arrives in streaming mode.
// Return  0 = continue
// Return -1 = abort (size limit, etc.)
typedef int (*body_chunk_cb_t)(void *udata, const uint8_t *chunk, size_t len);

typedef struct {
  Arena *arena;
  llhttp_t *parser;
  llhttp_settings_t *settings;

  // URL / method
  char *url;
  size_t url_length;
  size_t url_capacity;
  size_t path_length;

  char *method;
  size_t method_length;
  size_t method_capacity;

  // Headers
  request_t headers;
  request_t query_params;
  request_t url_params;

  // Body (buffered)
  uint8_t *body;
  size_t body_length;
  size_t body_capacity;

  // HTTP version / state
  uint8_t http_major;
  uint8_t http_minor;
  uint16_t status_code;

  bool message_complete;
  bool keep_alive;
  bool headers_complete;

  char *current_header_field;
  size_t header_field_length;
  size_t header_field_capacity;

  llhttp_errno_t last_error;
  const char *error_reason;

  // Streaming (opt-in)
  // Set by the body_stream middleware before the parser resumes
  // When non-NULL, on_body_cb delivers chunks here instead of buffering
  body_chunk_cb_t on_body_chunk;
  void *stream_udata;
} http_context_t;

// Used in router.c
parse_result_t http_parse_request(http_context_t *context, const char *data, size_t len);
bool http_message_needs_eof(const http_context_t *context);
parse_result_t http_finish_parsing(http_context_t *context);

// Used in server.c
void http_context_init(http_context_t *context,
                       Arena *arena,
                       llhttp_t *parser,
                       llhttp_settings_t *settings);

int on_url_cb(llhttp_t *parser, const char *at, size_t length);
int on_header_field_cb(llhttp_t *parser, const char *at, size_t length);
int on_header_value_cb(llhttp_t *parser, const char *at, size_t length);
int on_method_cb(llhttp_t *parser, const char *at, size_t length);
int on_body_cb(llhttp_t *parser, const char *at, size_t length);
int on_headers_complete_cb(llhttp_t *parser);
int on_message_complete_cb(llhttp_t *parser);

int ensure_array_capacity(Arena *arena, request_t *array);

// Utility function for debugging
const char *parse_result_to_string(parse_result_t result);

#endif
