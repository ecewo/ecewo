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

#include <stdarg.h>
#include "server.h"
#include "route-table.h"
#include "middleware.h"
#include "logger.h"

#define ROUTE_REGISTER(func_name, method_enum)                              \
  void func_name(ecewo_app_t *app, const char *path, int mw_count, ...) {         \
    if (!app || !app->server || !app->arena) {                            \
      LOG_ERROR("NULL app in route registration");                          \
      return;                                                               \
    }                                                                       \
                                                                            \
    if (!path) {                                                            \
      LOG_ERROR("NULL path in route registration");                         \
      return;                                                               \
    }                                                                       \
                                                                            \
    va_list args;                                                           \
    va_start(args, mw_count);                                               \
                                                                            \
    ecewo__middleware_t *mw = NULL;                                           \
    if (mw_count > 0) {                                                     \
      mw = ecewo_alloc(app->arena, sizeof(ecewo__middleware_t) * mw_count);   \
      if (!mw) {                                                            \
        LOG_ERROR("Middleware allocation failed");                          \
        va_end(args);                                                       \
        return;                                                             \
      }                                                                     \
                                                                            \
      for (int i = 0; i < mw_count; i++) {                                  \
        mw[i] = va_arg(args, ecewo__middleware_t);                            \
        if (!mw[i]) {                                                       \
          LOG_ERROR("NULL middleware handler at index %d", i);              \
          va_end(args);                                                     \
          return;                                                           \
        }                                                                   \
      }                                                                     \
    }                                                                       \
                                                                            \
    ecewo__handler_t handler = va_arg(args, ecewo__handler_t);                  \
    va_end(args);                                                           \
                                                                            \
    if (!handler) {                                                         \
      LOG_ERROR("NULL handler in route registration");                      \
      return;                                                               \
    }                                                                       \
                                                                            \
    MiddlewareInfo *info = ecewo_alloc(app->arena, sizeof(MiddlewareInfo)); \
    if (!info)                                                              \
      return;                                                               \
    memset(info, 0, sizeof(MiddlewareInfo));                                \
                                                                            \
    info->handler = handler;                                                \
    info->middleware_count = mw_count;                                      \
    info->middleware = mw;                                                  \
                                                                            \
    int result = route_table_add(app->server->route_table,                \
                                 method_enum, path, handler, info);         \
    if (result != 0)                                                        \
      LOG_ERROR("Failed to add route: %s", path);                           \
  }

ROUTE_REGISTER(ecewo__register_get, HTTP_GET) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo__register_post, HTTP_POST) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo__register_put, HTTP_PUT) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo__register_patch, HTTP_PATCH) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo__register_delete, HTTP_DELETE) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo__register_head, HTTP_HEAD) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo__register_options, HTTP_OPTIONS) // NOLINT(clang-analyzer-valist.Uninitialized)
