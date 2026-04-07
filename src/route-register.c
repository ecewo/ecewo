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

#include "server.h"
#include "route-table.h"
#include "middleware.h"
#include "logger.h"

typedef struct mw_node_s {
  ecewo_middleware_t fn;
  struct mw_node_s *next;
} mw_node_t;

struct ecewo_route_s {
  ecewo_app_t *app;
  ecewo_method_t method;
  const char *path;
  mw_node_t *mw_head;
  mw_node_t *mw_tail;
  int mw_count;
  bool committed;
};

ecewo_route_t *ecewo_route_new(ecewo_app_t *app, ecewo_method_t method, const char *path) {
  if (!app || !app->server || !app->arena) {
    LOG_ERROR("NULL app in ecewo_route_new");
    return NULL;
  }
  if (!path) {
    LOG_ERROR("NULL path in ecewo_route_new");
    return NULL;
  }

  ecewo_route_t *r = ecewo_alloc(app->arena, sizeof(ecewo_route_t));
  if (!r) {
    LOG_ERROR("Allocation failed in ecewo_route_new");
    return NULL;
  }
  memset(r, 0, sizeof(ecewo_route_t));
  r->app = app;
  r->method = method;
  r->path = path;
  return r;
}

void ecewo_route_middleware(ecewo_route_t *route, ecewo_middleware_t fn) {
  if (!route) {
    LOG_ERROR("NULL route in ecewo_route_middleware");
    return;
  }
  if (route->committed) {
    LOG_ERROR("ecewo_route_middleware called after ecewo_route_handler");
    return;
  }
  if (!fn) {
    LOG_ERROR("NULL middleware in ecewo_route_middleware");
    return;
  }

  mw_node_t *node = ecewo_alloc(route->app->arena, sizeof(mw_node_t));
  if (!node) {
    LOG_ERROR("Allocation failed in ecewo_route_middleware");
    return;
  }
  node->fn = fn;
  node->next = NULL;

  if (!route->mw_tail) {
    route->mw_head = node;
    route->mw_tail = node;
  } else {
    route->mw_tail->next = node;
    route->mw_tail = node;
  }
  route->mw_count++;
}

void ecewo_route_handler(ecewo_route_t *route, ecewo_handler_t handler) {
  if (!route) {
    LOG_ERROR("NULL route in ecewo_route_handler");
    return;
  }
  if (route->committed) {
    LOG_ERROR("ecewo_route_handler called twice on the same route");
    return;
  }
  if (!handler) {
    LOG_ERROR("NULL handler in ecewo_route_handler");
    return;
  }

  route->committed = true;

  ecewo_middleware_t *mw = NULL;
  if (route->mw_count > 0) {
    mw = ecewo_alloc(route->app->arena, sizeof(ecewo_middleware_t) * route->mw_count);
    if (!mw) {
      LOG_ERROR("Middleware allocation failed in ecewo_route_handler");
      return;
    }

    int i = 0;
    for (mw_node_t *n = route->mw_head; n; n = n->next)
      mw[i++] = n->fn;
  }

  MiddlewareInfo *info = ecewo_alloc(route->app->arena, sizeof(MiddlewareInfo));
  if (!info)
    return;
  memset(info, 0, sizeof(MiddlewareInfo));

  info->handler = handler;
  info->middleware_count = (uint16_t)route->mw_count;
  info->middleware = mw;

  int result = route_table_add(route->app->server->route_table,
                               (llhttp_method_t)route->method,
                               route->path, handler, info);
  if (result != 0)
    LOG_ERROR("Failed to add route: %s", route->path);
}

// Internal helper used by the ECEWO_GET/POST/... macros
// fns = [mw0, mw1, ..., handler], count = total elements
void ecewo_route_register(ecewo_app_t *app, ecewo_method_t method,
                          const char *path, void **fns, int count) {
  if (!fns || count < 1) {
    LOG_ERROR("No handler provided in route registration");
    return;
  }
  ecewo_route_t *r = ecewo_route_new(app, method, path);
  if (!r)
    return;
  for (int i = 0; i < count - 1; i++)
    ecewo_route_middleware(r, (ecewo_middleware_t)fns[i]);
  ecewo_route_handler(r, (ecewo_handler_t)fns[count - 1]);
}
