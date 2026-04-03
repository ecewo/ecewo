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

#include "uv.h"
#include "arena-internal.h"
#include "utils.h"
#include "logger.h"
#include "server.h"
#include <stdlib.h>
#include <ctype.h>

#ifdef ECEWO_DEBUG
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#define strcasecmp strcasecmp
#endif
#endif

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
  char *data;
  ecewo__client_t *client;
} write_req_t;

static void end_request(ecewo__client_t *client) {
  if (!client)
    return;

  client->request_in_progress = false;

  if (client->request_timeout_timer) {
    uv_timer_stop(client->request_timeout_timer);
    uv_close((uv_handle_t *)client->request_timeout_timer, (uv_close_cb)free);
    client->request_timeout_timer = NULL;
    ecewo_client_unref(client);
  }
}

static void write_completion_cb(uv_write_t *req, int status) {
  if (status < 0)
    LOG_ERROR("Write error: %s", uv_strerror(status));

  write_req_t *write_req = (write_req_t *)req;
  if (!write_req)
    return;

  if (write_req->client) {
    end_request(write_req->client);
    ecewo_client_unref(write_req->client);
  }

  if (write_req->data) {
    free(write_req->data);
    write_req->data = NULL;
  }

  free(write_req);
}

static bool validate_client_for_response(ecewo_response_t *res) {
  if (!res || !res->ecewo__client_socket || !res->ecewo__client_socket->data)
    return false;

  if (uv_is_closing((uv_handle_t *)res->ecewo__client_socket))
    return false;

  ecewo__client_t *client = (ecewo__client_t *)res->ecewo__client_socket->data;

  if (!client->valid || client->closing)
    return false;

  if (!uv_is_writable((uv_stream_t *)res->ecewo__client_socket))
    return false;

  return true;
}

// Sends 400 or 500
void send_error(ecewo_arena_t *arena, uv_tcp_t *ecewo__client_socket, int error_code) {
  if (!ecewo__client_socket) {
    if (arena)
      arena_reset(arena);
    return;
  }

  if (uv_is_closing((uv_handle_t *)ecewo__client_socket)) {
    if (arena)
      arena_reset(arena);
    return;
  }

  if (!uv_is_readable((uv_stream_t *)ecewo__client_socket) || !uv_is_writable((uv_stream_t *)ecewo__client_socket)) {
    if (arena)
      arena_reset(arena);
    return;
  }

  const char *date_str = get_cached_date();
  const char *status_text = (error_code == 500) ? "Internal Server Error" : "Bad Request";
  const char *body = status_text;
  size_t body_len = strlen(body);

  size_t response_size = 512;
  char *response = malloc(response_size);

  if (!response) {
    if (arena)
      arena_reset(arena);
    return;
  }

  int written = snprintf(response, response_size,
                         "HTTP/1.1 %d %s\r\n"
                         "Date: %s\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "%s",
                         error_code,
                         status_text,
                         date_str,
                         body_len,
                         body);

  if (written < 0 || (size_t)written >= response_size) {
    free(response);
    if (arena)
      arena_reset(arena);
    return;
  }

  write_req_t *write_req = malloc(sizeof(write_req_t));
  if (!write_req) {
    free(response);
    if (arena)
      arena_reset(arena);
    return;
  }

  memset(write_req, 0, sizeof(write_req_t));
  write_req->data = response;
  write_req->client = (ecewo__client_t *)ecewo__client_socket->data;
  if (write_req->client)
    ecewo_client_ref(write_req->client);
  write_req->buf = uv_buf_init(response, (unsigned int)written);

  int res = uv_write(&write_req->req, (uv_stream_t *)ecewo__client_socket,
                     &write_req->buf, 1, write_completion_cb);

  if (res < 0) {
    LOG_ERROR("Write error: %s", uv_strerror(res));
    free(response);

    if (write_req->client) {
      end_request(write_req->client);
      ecewo_client_unref(write_req->client);
    }

    free(write_req);
  }

  if (arena)
    arena_reset(arena);
}

void ecewo_send(ecewo_response_t *res, int status, const void *body, size_t body_len) {
  if (!res)
    return;

  res->replied = true;

  if (!validate_client_for_response(res)) {
    if (res->arena)
      arena_reset(res->arena);
    return;
  }

  if (!body)
    body_len = 0;

  size_t original_body_len = body_len;
  if (res->is_head_request) {
    body = NULL;
    body_len = 0;
  }

  const char *date_str = get_cached_date();
  const char *connection = res->keep_alive ? "keep-alive" : "close";

  size_t headers_size = 0;
  for (uint16_t i = 0; i < res->header_count; i++) {
    if (res->headers[i].name && res->headers[i].value) {
      headers_size += strlen(res->headers[i].name) + 2 + // "name: "
          strlen(res->headers[i].value) + 2; // "value\r\n"
    }
  }

  char *all_headers = NULL;
  if (headers_size > 0) {
    all_headers = ecewo_alloc(res->arena, headers_size + 1);
    if (!all_headers) {
      send_error(res->arena, res->ecewo__client_socket, 500);
      return;
    }

    size_t pos = 0;
    for (uint16_t i = 0; i < res->header_count; i++) {
      if (res->headers[i].name && res->headers[i].value) {
        size_t name_len = strlen(res->headers[i].name);
        size_t value_len = strlen(res->headers[i].value);

        memcpy(all_headers + pos, res->headers[i].name, name_len);
        pos += name_len;
        all_headers[pos++] = ':';
        all_headers[pos++] = ' ';
        memcpy(all_headers + pos, res->headers[i].value, value_len);
        pos += value_len;
        all_headers[pos++] = '\r';
        all_headers[pos++] = '\n';
      }
    }
    all_headers[pos] = '\0';
  } else {
    all_headers = ecewo_strdup(res->arena, "");
    if (!all_headers) {
      send_error(res->arena, res->ecewo__client_socket, 500);
      return;
    }
  }

  char *headers = ecewo_sprintf(res->arena,
                                "HTTP/1.1 %d\r\n"
                                "Date: %s\r\n"
                                "%s"
                                "Content-Length: %zu\r\n"
                                "Connection: %s\r\n"
                                "\r\n",
                                status,
                                date_str,
                                all_headers,
                                original_body_len,
                                connection);

  if (!headers) {
    send_error(res->arena, res->ecewo__client_socket, 500);
    return;
  }

  size_t headers_len = strlen(headers);
  size_t total_len = headers_len + body_len;

  char *response = malloc(total_len);
  if (!response) {
    send_error(res->arena, res->ecewo__client_socket, 500);
    return;
  }

  memcpy(response, headers, headers_len);
  if (body_len > 0 && body)
    memcpy(response + headers_len, body, body_len);

  // uv_write() is an async operation
  // so when ecewo_send() returns, client can send
  // another request and reset the arena,
  // but uv_write() might not be completed yet.
  // Therefore write_req must be
  // allocated via malloc, not ecewo_alloc!
  // Otherwise, it may cause segfault
  // under a high load.
  write_req_t *write_req = malloc(sizeof(write_req_t));
  if (!write_req) {
    free(response);
    send_error(res->arena, res->ecewo__client_socket, 500);
    return;
  }

  write_req->data = response;
  write_req->client = (ecewo__client_t *)res->ecewo__client_socket->data;
  if (write_req->client)
    ecewo_client_ref(write_req->client);
  write_req->buf = uv_buf_init(response, (unsigned int)total_len);

  if (uv_is_closing((uv_handle_t *)res->ecewo__client_socket)) {
    free(response);
    if (write_req->client)
      ecewo_client_unref(write_req->client);
    free(write_req);
    return;
  }

  int result = uv_write(&write_req->req, (uv_stream_t *)res->ecewo__client_socket,
                        &write_req->buf, 1, write_completion_cb);

  if (result < 0) {
    LOG_DEBUG("Write error: %s", uv_strerror(result));
    free(response);

    if (write_req->client) {
      end_request(write_req->client);
      ecewo_client_unref(write_req->client);
    }

    free(write_req);
  }

  if (res->arena)
    arena_reset(res->arena);
}

static bool is_valid_header_char(char c) {
  unsigned char uc = (unsigned char)c;

  if (uc == '\r' || uc == '\n')
    return false;

  return (uc == '\t') || (uc >= 32 && uc <= 126);
}

static bool is_valid_header_name(const char *name) {
  if (!name || !*name)
    return false;

  for (const char *p = name; *p; p++) {
    unsigned char c = *p;
    if (!(isalnum(c) || c == '-' || c == '_'))
      return false;
  }

  return true;
}

static bool is_valid_header_value(const char *value) {
  if (!value)
    return false;

  for (const char *p = value; *p; p++) {
    if (*p == '\r' || *p == '\n') {
      LOG_ERROR("Invalid character in header value: CRLF detected");
      return false;
    }

    if (!is_valid_header_char(*p)) {
      LOG_ERROR("Invalid character in header value: 0x%02x", (unsigned char)*p);
      return false;
    }
  }

  return true;
}

void ecewo_header_set(ecewo_response_t *res, const char *name, const char *value) {
  if (!res || !res->arena || !name || !value) {
    LOG_ERROR("Invalid argument(s) to ecewo_header_set");
    return;
  }

  if (!validate_client_for_response(res)) {
    if (res->arena)
      arena_reset(res->arena);
    return;
  }

  if (!is_valid_header_name(name)) {
    LOG_ERROR("Invalid header name: '%s'", name);
    return;
  }

  if (!is_valid_header_value(value)) {
    LOG_ERROR("Invalid header value for '%s'", name);
    return;
  }

#ifdef ECEWO_DEBUG
  // Check for duplicate headers and warn
  // but still add the header, do not override
  for (uint16_t i = 0; i < res->header_count; i++) {
    if (res->headers[i].name && strcasecmp(res->headers[i].name, name) == 0) {
      LOG_DEBUG("Warning: Duplicate header '%s' detected!", name);
      LOG_DEBUG("  Existing value: '%s'", res->headers[i].value);
      LOG_DEBUG("  New value: '%s'", value);
      LOG_DEBUG("  Both will be sent (this may cause issues)");
      break;
    }
  }
#endif

  if (res->header_count >= res->header_capacity) {
    uint16_t new_cap = res->header_capacity ? res->header_capacity * 2 : 8;

    ecewo__res_header_t *tmp = ecewo_realloc(res->arena, res->headers,
                                       res->header_capacity * sizeof(ecewo__res_header_t),
                                       new_cap * sizeof(ecewo__res_header_t));

    if (!tmp) {
      LOG_ERROR("Failed to realloc headers array");
      return;
    }

    memset(&tmp[res->header_capacity], 0,
           (new_cap - res->header_capacity) * sizeof(ecewo__res_header_t));

    res->headers = tmp;
    res->header_capacity = new_cap;
  }

  res->headers[res->header_count].name = ecewo_strdup(res->arena, name);
  res->headers[res->header_count].value = ecewo_strdup(res->arena, value);

  if (!res->headers[res->header_count].name || !res->headers[res->header_count].value) {
    LOG_ERROR("Failed to allocate memory in ecewo_header_set");
    return;
  }

  res->header_count++;
}

void ecewo_redirect(ecewo_response_t *res, int status, const char *url) {
  if (!res || !url)
    return;

  if (!validate_client_for_response(res)) {
    LOG_DEBUG("redirect(): Client validation failed");
    if (res->arena)
      arena_reset(res->arena);
    return;
  }

  if (!is_valid_header_value(url)) {
    LOG_ERROR("Invalid redirect URL (CRLF detected)");
    ecewo_send_text(res, BAD_REQUEST, "Bad Request");
    return;
  }

  ecewo_header_set(res, "Location", url);

  const char *message;
  size_t message_len;

  switch (status) {
  case MOVED_PERMANENTLY:
    message = "Moved Permanently";
    message_len = 17;
    break;
  case FOUND:
    message = "Found";
    message_len = 5;
    break;
  case SEE_OTHER:
    message = "See Other";
    message_len = 9;
    break;
  case TEMPORARY_REDIRECT:
    message = "Temporary Redirect";
    message_len = 18;
    break;
  case PERMANENT_REDIRECT:
    message = "Permanent Redirect";
    message_len = 18;
    break;
  default:
    message = "Redirect";
    message_len = 8;
    break;
  }

  ecewo_header_set(res, "Content-Type", "text/plain");
  ecewo_send(res, status, message, message_len);
}
