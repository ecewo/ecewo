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

#include <stdlib.h>
#include <stdbool.h>
#include "middleware.h"
#include "route-table.h"
#include "server.h"
#include "logger.h"

typedef struct {
  MiddlewareHandler *handlers;
  RequestHandler route_handler;
  uint16_t count;
  uint16_t current;
} Chain;

static bool path_matches_prefix(const char *prefix, const char *req_path) {
  if (!prefix)
    return true;

  size_t prefix_len = strlen(prefix);

  if (strncmp(req_path, prefix, prefix_len) != 0)
    return false;

  char next = req_path[prefix_len];
  return next == '\0' || next == '/' || prefix[prefix_len - 1] == '/';
}

static void execute_next(Req *req, Res *res) {
  if (!req || !res) {
    LOG_ERROR("NULL request or response");
    return;
  }

  Chain *chain = (Chain *)req->chain;

  if (!chain) {
    LOG_ERROR("NULL chain");
    return;
  }

  if (chain->current < chain->count) {
    MiddlewareHandler mw = chain->handlers[chain->current++];
    mw(req, res, execute_next);
  } else {
    if (chain->route_handler)
      chain->route_handler(req, res);
  }
}

void chain_start(Req *req, Res *res, MiddlewareInfo *middleware_info, struct server_t *srv) {
  if (!req || !res || !middleware_info || !middleware_info->handler)
    return;

  uint16_t matching_global = 0;

  if (srv) {
    for (uint16_t i = 0; i < srv->global_middleware_count; i++) {
      if (path_matches_prefix(srv->global_middleware[i].path_prefix, req->path))
        matching_global++;
    }
  }

  int total_middleware_count = matching_global + middleware_info->middleware_count;

  if (total_middleware_count == 0) {
    middleware_info->handler(req, res);
    return;
  }

  MiddlewareHandler *combined_handlers = ecewo_alloc(req->arena, sizeof(MiddlewareHandler) * total_middleware_count);

  if (!combined_handlers) {
    LOG_ERROR("Arena allocation failed for middleware handlers.");
    middleware_info->handler(req, res);
    return;
  }

  int idx = 0;

  if (srv) {
    for (uint16_t i = 0; i < srv->global_middleware_count; i++) {
      if (path_matches_prefix(srv->global_middleware[i].path_prefix, req->path))
        combined_handlers[idx++] = srv->global_middleware[i].handler;
    }
  }

  if (middleware_info->middleware_count > 0 && middleware_info->middleware) {
    memcpy(combined_handlers + idx,
           middleware_info->middleware,
           sizeof(MiddlewareHandler) * middleware_info->middleware_count);
  }

  Chain *chain = ecewo_alloc(req->arena, sizeof(Chain));
  if (!chain) {
    LOG_ERROR("Arena allocation failed for middleware chain.");
    middleware_info->handler(req, res);
    return;
  }

  chain->handlers = combined_handlers;
  chain->count = total_middleware_count;
  chain->current = 0;
  chain->route_handler = middleware_info->handler;

  req->chain = chain;

  execute_next(req, res);
}

void __ecewo_register_use(App *app, const char *path, MiddlewareHandler middleware_handler) {
  if (!middleware_handler) {
    LOG_ERROR("NULL middleware handler");
    abort();
  }

  if (!app || !app->internal) {
    LOG_ERROR("NULL app in __ecewo_register_use");
    abort();
  }

  struct server_t *srv = app->internal;

  if (srv->global_middleware_count >= srv->global_middleware_capacity) {
    int new_cap = srv->global_middleware_capacity
        ? srv->global_middleware_capacity * 2
        : INITIAL_MW_CAPACITY;
    GlobalMiddlewareEntry *tmp = realloc(srv->global_middleware, new_cap * sizeof *tmp);
    if (!tmp) {
      LOG_ERROR("Reallocation failed in global middleware");
      abort();
    }
    srv->global_middleware = tmp;
    srv->global_middleware_capacity = new_cap;
  }

  srv->global_middleware[srv->global_middleware_count].path_prefix = path;
  srv->global_middleware[srv->global_middleware_count].handler = middleware_handler;
  srv->global_middleware_count++;
}

void reset_middleware(struct server_t *srv) {
  if (!srv)
    return;

  if (srv->global_middleware) {
    free(srv->global_middleware);
    srv->global_middleware = NULL;
  }
  srv->global_middleware_count = 0;
  srv->global_middleware_capacity = 0;
}
