#ifndef ECEWO_HTTP_H
#define ECEWO_HTTP_H

#include "ecewo.h"
#include "llhttp.h"

typedef enum {
  PARSE_SUCCESS = 0, // Parsing completed successfully
  PARSE_INCOMPLETE = 1, // Need more data
  PARSE_ERROR = -1, // Parse error occurred
  PARSE_OVERFLOW = -2 // Buffer overflow or size limit exceeded
} parse_result_t;

typedef struct
{
  Arena *arena;
  llhttp_t *parser;
  llhttp_settings_t *settings;

  // Dynamic URL parsing state
  char *url;
  size_t url_length;
  size_t url_capacity;
  size_t path_length;

  char *method;
  size_t method_length;
  size_t method_capacity;

  request_t headers;
  request_t query_params;
  request_t url_params;

  char *body;
  size_t body_length;
  size_t body_capacity;

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

  // Body streaming support
  void *body_stream_ctx;        // BodyStreamCtx pointer
  bool body_streaming_enabled;  // True if streaming mode active
  bool body_paused;             // True if backpressure applied
} http_context_t;

// Using in router.c
parse_result_t http_parse_request(http_context_t *context, const char *data, size_t len);
bool http_message_needs_eof(const http_context_t *context);
parse_result_t http_finish_parsing(http_context_t *context);

// Using in server.c
void http_context_init(http_context_t *context,
                       Arena *arena,
                       llhttp_t *reused_parser,
                       llhttp_settings_t *reused_settings);
int on_url_cb(llhttp_t *parser, const char *at, size_t length);
int on_header_field_cb(llhttp_t *parser, const char *at, size_t length);
int on_header_value_cb(llhttp_t *parser, const char *at, size_t length);
int on_method_cb(llhttp_t *parser, const char *at, size_t length);
int on_body_cb(llhttp_t *parser, const char *at, size_t length);
int on_headers_complete_cb(llhttp_t *parser);
int on_message_complete_cb(llhttp_t *parser);

void http_context_init(http_context_t *context,
                       Arena *arena,
                       llhttp_t *reused_parser,
                       llhttp_settings_t *reused_settings);

// Utility function for debugging
const char *parse_result_to_string(parse_result_t result);

#endif
