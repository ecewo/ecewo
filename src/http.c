#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include "http.h"
#include "body.h"
#include "logger.h"

#define MIN_BUFFER_SIZE 64
#define GROWTH_FACTOR 1.5
#define MAX_SINGLE_ALLOCATION (10UL * 1024UL * 1024UL)
#define ABSOLUTE_MAX_REQUEST (50UL * 1024UL * 1024UL)
#define MAX_HEADER_SIZE (8UL * 1024UL)
#define MAX_URL_LENGTH 2048UL
#define MAX_METHOD_LENGTH 16U
#define MAX_HEADERS_COUNT 100
#define MAX_QUERY_PARAMS 100

#define ERROR_REASON_URL_TOO_LONG "URL exceeds maximum allowed length"
#define ERROR_REASON_HEADER_TOO_LARGE "HTTP header field or value too large"
#define ERROR_REASON_TOO_MANY_HEADERS "Too many HTTP headers"
#define ERROR_REASON_METHOD_TOO_LONG "HTTP method name too long"
#define ERROR_REASON_PAYLOAD_TOO_LARGE "Request payload exceeds maximum size"
#define ERROR_REASON_INVALID_HEADER_FIELD "Invalid or missing header field"
#define ERROR_REASON_MEMORY_ALLOCATION "Memory allocation failed"
#define ERROR_REASON_INVALID_METHOD "Invalid HTTP method"

static size_t calculate_next_size(size_t current, size_t needed) {
  if (needed > ABSOLUTE_MAX_REQUEST) {
    LOG_DEBUG("Request too large: %zu bytes", needed);
    return 0;
  }

  if (needed <= current)
    return current;

  size_t new_size = current < MIN_BUFFER_SIZE ? MIN_BUFFER_SIZE : current;

  while (new_size < needed) {
    if (new_size > (size_t)(SIZE_MAX / GROWTH_FACTOR)) {
      new_size = needed + MIN_BUFFER_SIZE;
      break;
    }

    size_t next = (size_t)(new_size * GROWTH_FACTOR);
    if (next <= new_size) {
      new_size = needed + MIN_BUFFER_SIZE;
      break;
    }
    new_size = next;
  }

  if (new_size > MAX_SINGLE_ALLOCATION && needed <= MAX_SINGLE_ALLOCATION)
    new_size = MAX_SINGLE_ALLOCATION;

  return new_size > ABSOLUTE_MAX_REQUEST ? ABSOLUTE_MAX_REQUEST : new_size;
}

static int ensure_buffer_capacity(Arena *arena, char **buffer, size_t *capacity, size_t current_length, size_t additional_needed) {
  if (!arena || !buffer || !capacity)
    return -1;

  size_t total_needed = current_length + additional_needed + 1;

  if (total_needed <= *capacity)
    return 0;

  if (total_needed > ABSOLUTE_MAX_REQUEST) {
    LOG_DEBUG("Request exceeds maximum size: %zu bytes", total_needed);
    return -2;
  }

  size_t new_capacity = calculate_next_size(*capacity, total_needed);
  if (new_capacity == 0 || new_capacity < total_needed)
    return -2;

  char *new_buffer = arena_realloc(arena, *buffer, *capacity, new_capacity);
  if (!new_buffer)
    return -1;

  *buffer = new_buffer;
  *capacity = new_capacity;
  return 0;
}

static void parse_query(Arena *arena, const char *query_start, size_t query_len, request_t *query) {
  if (!arena || !query)
    return;

  memset(query, 0, sizeof(request_t));

  if (!query_start || query_len == 0)
    return;

  int param_count = 1;
  for (size_t i = 0; i < query_len; i++) {
    if (query_start[i] == '&') {
      param_count++;
      if (param_count > MAX_QUERY_PARAMS) {
        param_count = MAX_QUERY_PARAMS;
        break;
      }
    }
  }

  query->capacity = param_count;
  query->items = arena_alloc(arena, query->capacity * sizeof(request_item_t));
  if (!query->items) {
    query->capacity = 0;
    return;
  }

  const char *p = query_start;
  const char *end = query_start + query_len;

  while (p < end && query->count < query->capacity) {
    const char *key_start = p;
    const char *eq = NULL;
    const char *amp = NULL;

    // Find = and &
    for (const char *s = p; s < end; s++) {
      if (*s == '=' && !eq)
        eq = s;
      if (*s == '&') {
        amp = s;
        break;
      }
    }

    const char *pair_end = amp ? amp : end;

    if (eq && eq < pair_end) {
      size_t key_len = eq - key_start;
      size_t val_len = pair_end - (eq + 1);

      char *key = arena_alloc(arena, key_len + 1);
      if (key) {
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';
        query->items[query->count].key = key;
      }

      if (val_len > 0) {
        char *value = arena_alloc(arena, val_len + 1);
        if (value) {
          memcpy(value, eq + 1, val_len);
          value[val_len] = '\0';
          query->items[query->count].value = value;
        }
      } else {
        query->items[query->count].value = NULL;
      }

      if (query->items[query->count].key)
        query->count++;
    }

    p = amp ? amp + 1 : end;
  }
}

int on_url_cb(llhttp_t *parser, const char *at, size_t length) {
  if (!parser || !parser->data || !at || length == 0)
    return HPE_INTERNAL;

  http_context_t *context = (http_context_t *)parser->data;

  if (context->url_length + length > MAX_URL_LENGTH) {
    llhttp_set_error_reason(parser, ERROR_REASON_URL_TOO_LONG);
    return HPE_USER;
  }

  int result = ensure_buffer_capacity(context->arena,
                                      &context->url,
                                      &context->url_capacity,
                                      context->url_length,
                                      length);
  if (result == -2) {
    llhttp_set_error_reason(parser, ERROR_REASON_URL_TOO_LONG);
    return HPE_USER;
  } else if (result != 0) {
    llhttp_set_error_reason(parser, ERROR_REASON_MEMORY_ALLOCATION);
    return HPE_INTERNAL;
  }

  memmove(context->url + context->url_length, at, length);
  context->url_length += length;
  context->url[context->url_length] = '\0';

  return HPE_OK;
}

int on_header_field_cb(llhttp_t *parser, const char *at, size_t length) {
  if (!parser || !parser->data || !at || length == 0)
    return HPE_INTERNAL;

  http_context_t *context = (http_context_t *)parser->data;

  if (context->headers.count >= MAX_HEADERS_COUNT) {
    llhttp_set_error_reason(parser, ERROR_REASON_TOO_MANY_HEADERS);
    return HPE_USER;
  }

  if (length > MAX_HEADER_SIZE) {
    llhttp_set_error_reason(parser, ERROR_REASON_HEADER_TOO_LARGE);
    return HPE_USER;
  }

  context->header_field_length = 0;

  int result = ensure_buffer_capacity(context->arena, &context->current_header_field,
                                      &context->header_field_capacity, 0, length);
  if (result == -2) {
    llhttp_set_error_reason(parser, ERROR_REASON_HEADER_TOO_LARGE);
    return HPE_USER;
  } else if (result != 0) {
    llhttp_set_error_reason(parser, ERROR_REASON_MEMORY_ALLOCATION);
    return HPE_INTERNAL;
  }

  memmove(context->current_header_field, at, length);
  context->header_field_length = length;
  context->current_header_field[length] = '\0';

  return HPE_OK;
}

int ensure_array_capacity(Arena *arena, request_t *array) {
  if (!arena || !array)
    return -1;

  if (array->count < array->capacity)
    return 0;

  if (array->capacity > MAX_HEADERS_COUNT / 2)
    return -1;

  int new_capacity = array->capacity == 0 ? 16 : array->capacity * 2;

  if (new_capacity > MAX_HEADERS_COUNT)
    new_capacity = MAX_HEADERS_COUNT;

  if (new_capacity <= array->capacity)
    return -1;

  size_t old_size = array->capacity * sizeof(request_item_t);
  size_t new_size = new_capacity * sizeof(request_item_t);

  request_item_t *new_items = arena_realloc(arena, array->items, old_size, new_size);
  if (!new_items)
    return -1;

  for (int i = array->capacity; i < new_capacity; i++) {
    new_items[i].key = NULL;
    new_items[i].value = NULL;
  }

  array->items = new_items;
  array->capacity = new_capacity;
  return 0;
}

int on_header_value_cb(llhttp_t *parser, const char *at, size_t length) {
  if (!parser || !parser->data || !at || length == 0)
    return HPE_INTERNAL;

  http_context_t *context = (http_context_t *)parser->data;

  if (context->header_field_length == 0) {
    llhttp_set_error_reason(parser, ERROR_REASON_INVALID_HEADER_FIELD);
    return HPE_USER;
  }

  if (ensure_array_capacity(context->arena, &context->headers) != 0) {
    llhttp_set_error_reason(parser, ERROR_REASON_MEMORY_ALLOCATION);
    return HPE_INTERNAL;
  }

  char *key_copy = arena_alloc(context->arena, context->header_field_length + 1);
  if (!key_copy) {
    llhttp_set_error_reason(parser, ERROR_REASON_MEMORY_ALLOCATION);
    return HPE_INTERNAL;
  }

  memcpy(key_copy, context->current_header_field, context->header_field_length);
  key_copy[context->header_field_length] = '\0';

  char *value_copy = arena_alloc(context->arena, length + 1);
  if (!value_copy) {
    llhttp_set_error_reason(parser, ERROR_REASON_MEMORY_ALLOCATION);
    return HPE_INTERNAL;
  }

  memcpy(value_copy, at, length);
  value_copy[length] = '\0';

  context->headers.items[context->headers.count].key = key_copy;
  context->headers.items[context->headers.count].value = value_copy;
  context->headers.count++;

  return HPE_OK;
}

int on_method_cb(llhttp_t *parser, const char *at, size_t length) {
  if (!parser || !parser->data || !at || length == 0)
    return HPE_INTERNAL;

  http_context_t *context = (http_context_t *)parser->data;

  if (context->method_length + length > MAX_METHOD_LENGTH) {
    llhttp_set_error_reason(parser, ERROR_REASON_METHOD_TOO_LONG);
    return HPE_USER;
  }

  int result = ensure_buffer_capacity(context->arena,
                                      &context->method,
                                      &context->method_capacity,
                                      context->method_length,
                                      length);
  if (result == -2) {
    llhttp_set_error_reason(parser, ERROR_REASON_METHOD_TOO_LONG);
    return HPE_USER;
  } else if (result != 0) {
    llhttp_set_error_reason(parser, ERROR_REASON_MEMORY_ALLOCATION);
    return HPE_INTERNAL;
  }

  memmove(context->method + context->method_length, at, length);
  context->method_length += length;
  context->method[context->method_length] = '\0';

  return HPE_OK;
}

int on_body_cb(llhttp_t *parser, const char *at, size_t length) {
  if (!parser || !parser->data || !at || length == 0)
    return HPE_INTERNAL;

  http_context_t *context = (http_context_t *)parser->data;

  if (context->body_streaming_enabled && context->body_stream_ctx) {
    int result = body_stream_on_chunk(context->body_stream_ctx, at, length);
    
    if (result < 0) {
      llhttp_set_error_reason(parser, ERROR_REASON_PAYLOAD_TOO_LARGE);
      return HPE_USER;
    }
    
    if (result == 1) {
      context->body_paused = true;
      LOG_DEBUG("Body parsing paused - backpressure");
      return HPE_PAUSED;
    }
    
    return HPE_OK;
  }

  // Default buffering
  int result = ensure_buffer_capacity(context->arena,
                                      &context->body,
                                      &context->body_capacity,
                                      context->body_length,
                                      length);

  if (result == -2) {
    llhttp_set_error_reason(parser, ERROR_REASON_PAYLOAD_TOO_LARGE);
    return HPE_USER; // Payload too large
  } else if (result != 0) {
    llhttp_set_error_reason(parser, ERROR_REASON_MEMORY_ALLOCATION);
    return HPE_INTERNAL;
  }

  memmove(context->body + context->body_length, at, length);
  context->body_length += length;

  return HPE_OK;
}

int on_headers_complete_cb(llhttp_t *parser) {
  if (!parser || !parser->data)
    return HPE_INTERNAL;

  http_context_t *context = (http_context_t *)parser->data;

  context->http_major = llhttp_get_http_major(parser);
  context->http_minor = llhttp_get_http_minor(parser);
  context->keep_alive = llhttp_should_keep_alive(parser);
  context->headers_complete = 1;

  // Signal that headers are complete
  // This allows streaming handlers to start processing
  // Note: Router will need to handle this signal for early invocation
  if (context->body_streaming_enabled && context->body_stream_ctx) {
    LOG_DEBUG("Headers complete - streaming mode ready");
    // Router will detect headers_complete=1 and invoke handler early
  }

  return HPE_OK;
}

int on_message_complete_cb(llhttp_t *parser) {
  if (!parser || !parser->data)
    return HPE_INTERNAL;

  http_context_t *context = (http_context_t *)parser->data;
  context->message_complete = 1;

  if (context->url && context->url_length > 0) {
    const char *qmark = memchr(context->url, '?', context->url_length);
    if (qmark) {
      context->path_length = qmark - context->url;
      size_t query_len = context->url_length - context->path_length - 1;
      parse_query(context->arena, qmark + 1, query_len, &context->query_params);
    } else {
      context->path_length = context->url_length;
    }
  }

  return HPE_OK;
}

void http_context_init(http_context_t *context,
                       Arena *arena,
                       llhttp_t *reused_parser,
                       llhttp_settings_t *reused_settings) {
  if (!context || !arena || !reused_parser || !reused_settings)
    return;

  memset(context, 0, sizeof(http_context_t));
  context->arena = arena;

  context->parser = reused_parser;
  context->settings = reused_settings;

  context->parser->data = context;

  context->url_capacity = 512;
  context->url = arena_alloc(arena, context->url_capacity);
  context->url_length = 0;
  if (context->url)
    context->url[0] = '\0';

  context->method_capacity = 32;
  context->method = arena_alloc(arena, context->method_capacity);
  context->method_length = 0;
  if (context->method)
    context->method[0] = '\0';

  context->header_field_capacity = 128;
  context->current_header_field = arena_alloc(arena, context->header_field_capacity);
  context->header_field_length = 0;
  if (context->current_header_field)
    context->current_header_field[0] = '\0';

  context->body_capacity = 1024;
  context->body = arena_alloc(arena, context->body_capacity);
  context->body_length = 0;
  if (context->body)
    context->body[0] = '\0';

  context->headers.count = 0;
  context->headers.capacity = 32;
  context->headers.items = arena_alloc(arena, context->headers.capacity * sizeof(request_item_t));
  if (context->headers.items)
    memset(context->headers.items, 0, context->headers.capacity * sizeof(request_item_t));

  memset(&context->query_params, 0, sizeof(request_t));
  memset(&context->url_params, 0, sizeof(request_t));

  context->keep_alive = 1;
  context->message_complete = 0;
  context->headers_complete = 0;
  context->last_error = HPE_OK;
  context->error_reason = NULL;

  context->body_stream_ctx = NULL;
  context->body_streaming_enabled = false;
  context->body_paused = false;
}

parse_result_t http_parse_request(http_context_t *context, const char *data, size_t len) {
  if (!context || !data || len == 0)
    return PARSE_ERROR;

  llhttp_errno_t err = llhttp_execute(context->parser, data, len);

  context->last_error = err;
  context->error_reason = llhttp_get_error_reason(context->parser);

  switch (err) {
  case HPE_OK:
    if (context->message_complete)
      return PARSE_SUCCESS;
    return PARSE_INCOMPLETE;

  case HPE_PAUSED:
    return PARSE_INCOMPLETE;

  case HPE_PAUSED_UPGRADE:
    if (context->message_complete)
      return PARSE_SUCCESS;
    return PARSE_INCOMPLETE;

  case HPE_USER:
    // User-defined errors (size limits, etc.)
    return PARSE_OVERFLOW;

  default:
    return PARSE_ERROR;
  }
}

bool http_message_needs_eof(const http_context_t *context) {
  if (!context)
    return 0;

  return llhttp_message_needs_eof(context->parser) != 0;
}

parse_result_t http_finish_parsing(http_context_t *context) {
  if (!context)
    return PARSE_ERROR;

  llhttp_errno_t err = llhttp_finish(context->parser);

  context->last_error = err;
  context->error_reason = llhttp_get_error_reason(context->parser);

  switch (err) {
  case HPE_OK:
    return PARSE_SUCCESS;
  case HPE_USER:
    return PARSE_OVERFLOW;
  default:
    return PARSE_ERROR;
  }
}

const char *parse_result_to_string(parse_result_t result) {
  switch (result) {
  case PARSE_SUCCESS:
    return "PARSE_SUCCESS";
  case PARSE_INCOMPLETE:
    return "PARSE_INCOMPLETE";
  case PARSE_ERROR:
    return "PARSE_ERROR";
  case PARSE_OVERFLOW:
    return "PARSE_OVERFLOW";
  default:
    return "UNKNOWN";
  }
}
