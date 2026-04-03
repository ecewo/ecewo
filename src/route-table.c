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
#include <inttypes.h>
#include "route-table.h"
#include "middleware.h"
#include "logger.h"
#include "rax.h"

#define MAX_PATH_SEGMENTS 128
#define METHOD_COUNT 7

typedef enum {
  METHOD_INDEX_DELETE,
  METHOD_INDEX_GET,
  METHOD_INDEX_HEAD,
  METHOD_INDEX_POST,
  METHOD_INDEX_PUT,
  METHOD_INDEX_OPTIONS,
  METHOD_INDEX_PATCH
} http_method_index_t;

static int method_to_index(llhttp_method_t method) {
  switch (method) {
  case HTTP_DELETE:
    return METHOD_INDEX_DELETE;
  case HTTP_GET:
    return METHOD_INDEX_GET;
  case HTTP_HEAD:
    return METHOD_INDEX_HEAD;
  case HTTP_POST:
    return METHOD_INDEX_POST;
  case HTTP_PUT:
    return METHOD_INDEX_PUT;
  case HTTP_OPTIONS:
    return METHOD_INDEX_OPTIONS;
  case HTTP_PATCH:
    return METHOD_INDEX_PATCH;
  default:
    return -1;
  }
}

// -------------------------------------------------------------------------
// Segment-level radix tree
// -------------------------------------------------------------------------
//
// Each route_node_t represents a position in the URL path. It can have:
//
//   children:   a rax mapping literal segment text -> child route_node_t*
//   param_child: a single child for ':param' segments (captures one segment)
//   wildcard_handlers: per-method handlers for '*' (matches all remaining)
//   handlers:   per-method endpoint handlers (when this node terminates a route)
//
// Matching priority at each level: literal > param > wildcard.
// The tree naturally backtracks when a greedy literal choice leads to a dead
// end, ensuring the most specific route always wins.
//
// Edge-case behaviours (verified by tests/test-router-edge.c):
//
//  1. Bare ":" (empty param name)
//     Route "/:": the param_name is "" (length 0).
//     get_param(req, "") retrieves the value.
//
//  2. Wildcard not at the end of a pattern ("/prefix/*/suffix")
//     During registration, when a '*' segment is encountered, the wildcard
//     handler is stored on the current node and remaining segments are
//     ignored.  Effectively "/a/*/b" behaves identically to "/a/*".
//
//  3. Percent-encoded characters ("%2F", "%20", ...)
//     The routing layer operates on the raw URL bytes as delivered by
//     llhttp; it performs NO percent-decoding. "%2F" is three literal
//     bytes, not a path separator.
//     Param values are decoded after extraction (like decodeURIComponent).

typedef struct route_node {
  rax *children;
  struct route_node *param_child;

  ecewo__handler_t handlers[METHOD_COUNT];
  void *middleware_ctx[METHOD_COUNT];
  char **route_param_names[METHOD_COUNT];
  uint8_t route_param_count[METHOD_COUNT];

  ecewo__handler_t wildcard_handlers[METHOD_COUNT];
  void *wildcard_middleware_ctx[METHOD_COUNT];
  char **wildcard_param_names[METHOD_COUNT];
  uint8_t wildcard_param_count[METHOD_COUNT];
} route_node_t;

struct route_table {
  route_node_t *root;
  size_t route_count;
};

// -------------------------------------------------------------------------
// Tokenizers (shared with router.c)
// -------------------------------------------------------------------------

// Called by router.c before route_table_match
int tokenize_path(ecewo_arena_t *arena, const char *path, size_t path_len, tokenized_path_t *result) {
  if (!path || !result)
    return -1;

  memset(result, 0, sizeof(tokenized_path_t));

  if (path_len > 0 && *path == '/') {
    path++;
    path_len--;
  }

  if (path_len == 0)
    return 0;

  uint8_t segment_count = 0;
  const char *p = path;
  const char *end = path + path_len;

  path_segment_t segments[MAX_PATH_SEGMENTS];

  while (p < end) {
    while (p < end && *p == '/')
      p++;

    if (p >= end)
      break;

    const char *start = p;

    while (p < end && *p != '/')
      p++;

    size_t len = (size_t)(p - start);
    if (len == 0)
      continue;

    if (segment_count >= MAX_PATH_SEGMENTS) {
      LOG_DEBUG("Path too deep: %" PRIu8 " segments (max %d)",
                segment_count, MAX_PATH_SEGMENTS);
      return -1;
    }

    segments[segment_count].start = start;
    segments[segment_count].len = len;
    segments[segment_count].is_param = (start[0] == ':');
    segments[segment_count].is_wildcard = (start[0] == '*');

    segment_count++;
  }

  if (segment_count == 0)
    return 0;

  result->count = segment_count;
  result->segments = ecewo_alloc(arena, sizeof(path_segment_t) * segment_count);
  if (!result->segments)
    return -1;

  memcpy(result->segments, segments,
         segment_count * sizeof(path_segment_t));

  return 0;
}

// Tokenize a route pattern at registration time
// The caller owns *segs_out and *buf_out and must free both
static int tokenize_pattern(const char *path,
                            path_segment_t **segs_out,
                            uint8_t *count_out,
                            char **buf_out) {
  char *buf = strdup(path);
  if (!buf)
    return -1;

  const char *p = buf;
  const char *end = buf + strlen(buf);

  if (p < end && *p == '/')
    p++;

  path_segment_t segs[MAX_PATH_SEGMENTS];
  uint8_t count = 0;

  while (p < end) {
    while (p < end && *p == '/')
      p++;
    if (p >= end)
      break;

    const char *start = p;
    while (p < end && *p != '/')
      p++;

    size_t len = (size_t)(p - start);
    if (len == 0)
      continue;

    if (count >= MAX_PATH_SEGMENTS) {
      free(buf);
      return -1;
    }

    segs[count].start = start;
    segs[count].len = len;
    segs[count].is_param = (start[0] == ':');
    segs[count].is_wildcard = (start[0] == '*');
    count++;
  }

  path_segment_t *result = malloc(sizeof(path_segment_t) * (count > 0 ? count : 1));
  if (!result) {
    free(buf);
    return -1;
  }
  memcpy(result, segs, sizeof(path_segment_t) * count);

  *segs_out = result;
  *count_out = count;
  *buf_out = buf;
  return 0;
}

// -------------------------------------------------------------------------
// Param capture
// -------------------------------------------------------------------------

static int add_param_to_match(route_match_t *match,
                              ecewo_arena_t *arena,
                              const char *key_data,
                              size_t key_len,
                              const char *value_data,
                              size_t value_len) {
  if (!match)
    return -1;

  // Inline storage
  if (match->param_count < MAX_INLINE_PARAMS && !match->params) {
    param_match_t *param = &match->inline_params[match->param_count];
    param->key.data = key_data;
    param->key.len = key_len;
    param->value.data = value_data;
    param->value.len = value_len;
    match->param_count++;
    return 0;
  }

  // Switch to arena-allocated storage
  if (match->param_count == MAX_INLINE_PARAMS && !match->params) {
    uint8_t new_cap = MAX_INLINE_PARAMS * 2;
    param_match_t *new_params = ecewo_alloc(arena, sizeof(param_match_t) * new_cap);
    if (!new_params) {
      LOG_ERROR("Failed to allocate dynamic param storage");
      return -1;
    }
    memcpy(new_params, match->inline_params, sizeof(param_match_t) * MAX_INLINE_PARAMS);
    match->params = new_params;
    match->param_capacity = new_cap;
    LOG_DEBUG("Route params overflow: switched to dynamic allocation (%d params)", new_cap);
  }

  // Grow arena storage if needed
  if (match->params && match->param_count >= match->param_capacity) {
    uint8_t new_cap = match->param_capacity * 2;
    if (new_cap > 64) {
      LOG_ERROR("Route parameter limit exceeded: %d", new_cap);
      return -1;
    }
    param_match_t *new_params = ecewo_realloc(arena,
                                              match->params,
                                              sizeof(param_match_t) * match->param_capacity,
                                              sizeof(param_match_t) * new_cap);
    if (!new_params) {
      LOG_ERROR("Failed to reallocate param storage");
      return -1;
    }
    match->params = new_params;
    match->param_capacity = new_cap;
  }

  if (!match->params) {
    LOG_ERROR("Unexpected NULL params pointer with param_count=%d", match->param_count);
    return -1;
  }

  param_match_t *target = &match->params[match->param_count];
  target->key.data = key_data;
  target->key.len = key_len;
  target->value.data = value_data;
  target->value.len = value_len;
  match->param_count++;
  return 0;
}

// -------------------------------------------------------------------------
// Node management
// -------------------------------------------------------------------------

static route_node_t *route_node_create(void) {
  return calloc(1, sizeof(route_node_t));
}

static void route_node_free_cb(void *data);

static void route_node_free(route_node_t *node) {
  if (!node)
    return;

  for (int i = 0; i < METHOD_COUNT; i++) {
    if (node->route_param_names[i]) {
      for (uint8_t j = 0; j < node->route_param_count[i]; j++)
        free(node->route_param_names[i][j]);
      free(node->route_param_names[i]);
    }
    if (node->wildcard_param_names[i]) {
      for (uint8_t j = 0; j < node->wildcard_param_count[i]; j++)
        free(node->wildcard_param_names[i][j]);
      free(node->wildcard_param_names[i]);
    }
  }

  route_node_free(node->param_child);

  if (node->children)
    raxFreeWithCallback(node->children, route_node_free_cb);

  free(node);
}

static void route_node_free_cb(void *data) {
  route_node_free((route_node_t *)data);
}

// -------------------------------------------------------------------------
// Tree matching (recursive with backtracking)
// -------------------------------------------------------------------------

// allow_wildcards controls whether wildcard handlers are considered.
// route_table_match runs two passes: first without wildcards (so endpoint
// matches via params always beat wildcard-zero matches), then with wildcards.
//
// leaf_out: on success, set to the node that owns the matched handler so the
// caller can retrieve the per-route param name table.
static bool match_node(route_node_t *node,
                       const tokenized_path_t *path,
                       uint8_t seg_idx,
                       int method_idx,
                       route_match_t *match,
                       ecewo_arena_t *arena,
                       bool allow_wildcards,
                       route_node_t **leaf_out) {
  // All segments consumed
  // Check for an endpoint handler
  if (seg_idx == path->count) {
    if (node->handlers[method_idx]) {
      match->handler = node->handlers[method_idx];
      match->middleware_ctx = node->middleware_ctx[method_idx];
      *leaf_out = node;
      return true;
    }
    // Wildcard matches zero remaining segments too
    if (allow_wildcards && node->wildcard_handlers[method_idx]) {
      match->handler = node->wildcard_handlers[method_idx];
      match->middleware_ctx = node->wildcard_middleware_ctx[method_idx];
      *leaf_out = node;
      return true;
    }
    return false;
  }

  const path_segment_t *seg = &path->segments[seg_idx];

  // 1. Literal child (highest priority)
  if (node->children) {
    void *child = raxFind(node->children, (unsigned char *)seg->start, seg->len);
    if (child != raxNotFound) {
      if (match_node((route_node_t *)child, path, seg_idx + 1, method_idx, match, arena, allow_wildcards, leaf_out))
        return true;
    }
  }

  // 2. Param child
  if (node->param_child) {
    uint8_t saved = match->param_count;
    if (add_param_to_match(match, arena,
                           NULL, 0,
                           seg->start, seg->len)
        == 0) {
      if (match_node(node->param_child, path, seg_idx + 1, method_idx, match, arena, allow_wildcards, leaf_out))
        return true;
    }
    match->param_count = saved;
  }

  // 3. Wildcard (matches all remaining segments)
  if (allow_wildcards && node->wildcard_handlers[method_idx]) {
    match->handler = node->wildcard_handlers[method_idx];
    match->middleware_ctx = node->wildcard_middleware_ctx[method_idx];
    *leaf_out = node;
    return true;
  }

  return false;
}

// -------------------------------------------------------------------------
// Method collection (for 405 Allow header)
// -------------------------------------------------------------------------

// Returns a bitmask of which method indices have
// a registered handler for the given path.
// Wildcard handlers at any node along the path also count.
static uint8_t collect_methods(route_node_t *node,
                               const tokenized_path_t *path,
                               uint8_t seg_idx) {
  if (!node)
    return 0;

  uint8_t mask = 0;

  if (seg_idx == path->count) {
    for (int i = 0; i < METHOD_COUNT; i++) {
      if (node->handlers[i])
        mask |= (uint8_t)(1u << i);
      // Wildcard matches zero remaining segments too
      if (node->wildcard_handlers[i])
        mask |= (uint8_t)(1u << i);
    }
    return mask;
  }

  const path_segment_t *seg = &path->segments[seg_idx];

  // Wildcard at this node matches all remaining segments
  for (int i = 0; i < METHOD_COUNT; i++) {
    if (node->wildcard_handlers[i])
      mask |= (uint8_t)(1u << i);
  }

  // Literal child
  if (node->children) {
    void *child = raxFind(node->children, (unsigned char *)seg->start, seg->len);
    if (child != raxNotFound)
      mask |= collect_methods((route_node_t *)child, path, seg_idx + 1);
  }

  // Param child
  if (node->param_child)
    mask |= collect_methods(node->param_child, path, seg_idx + 1);

  return mask;
}

uint8_t route_table_allowed_methods(route_table_t *table,
                                    const tokenized_path_t *path) {
  if (!table || !table->root || !path)
    return 0;
  return collect_methods(table->root, path, 0);
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

route_table_t *route_table_create(void) {
  route_table_t *table = calloc(1, sizeof(route_table_t));
  if (!table)
    return NULL;
  table->root = route_node_create();
  if (!table->root) {
    free(table);
    return NULL;
  }
  return table;
}

int route_table_add(route_table_t *table,
                    llhttp_method_t method,
                    const char *path,
                    ecewo__handler_t handler,
                    void *middleware_ctx) {
  if (!table || !path || !handler)
    return -1;

  int method_idx = method_to_index(method);
  if (method_idx < 0) {
    LOG_DEBUG("Unsupported HTTP method: %d", method);
    return -1;
  }

  path_segment_t *segs;
  uint8_t seg_count;
  char *pattern_buf;

  if (tokenize_pattern(path, &segs, &seg_count, &pattern_buf) != 0)
    return -1;

  route_node_t *node = table->root;

  const char *collected_names[MAX_PATH_SEGMENTS];
  size_t collected_name_lens[MAX_PATH_SEGMENTS];
  uint8_t collected_count = 0;

  for (uint8_t i = 0; i < seg_count; i++) {
    if (segs[i].is_wildcard) {
      // '*' consumes all remaining segments; anything after it is ignored
      // Store the param names collected so far for this wildcard handler.
      char **names = NULL;
      if (collected_count > 0) {
        names = malloc(sizeof(char *) * collected_count);
        if (!names) {
          free(segs);
          free(pattern_buf);
          return -1;
        }
        for (uint8_t j = 0; j < collected_count; j++) {
          names[j] = malloc(collected_name_lens[j] + 1);
          if (!names[j]) {
            for (uint8_t k = 0; k < j; k++)
              free(names[k]);
            free(names);
            free(segs);
            free(pattern_buf);
            return -1;
          }
          memcpy(names[j], collected_names[j], collected_name_lens[j]);
          names[j][collected_name_lens[j]] = '\0';
        }
      }

      // Free old wildcard param names if re-registering
      if (node->wildcard_param_names[method_idx]) {
        for (uint8_t j = 0; j < node->wildcard_param_count[method_idx]; j++)
          free(node->wildcard_param_names[method_idx][j]);
        free(node->wildcard_param_names[method_idx]);
      }
      node->wildcard_handlers[method_idx] = handler;
      node->wildcard_middleware_ctx[method_idx] = middleware_ctx;
      node->wildcard_param_names[method_idx] = names;
      node->wildcard_param_count[method_idx] = collected_count;
      free(segs);
      free(pattern_buf);
      table->route_count++;
      return 0;
    }

    if (segs[i].is_param) {
      collected_names[collected_count] = segs[i].start + 1; // skip ':'
      collected_name_lens[collected_count] = segs[i].len - 1;
      collected_count++;

      if (!node->param_child) {
        node->param_child = route_node_create();
        if (!node->param_child) {
          free(segs);
          free(pattern_buf);
          return -1;
        }
      }
      node = node->param_child;

    } else {
      if (!node->children) {
        node->children = raxNew();
        if (!node->children) {
          free(segs);
          free(pattern_buf);
          return -1;
        }
      }

      void *child = raxFind(node->children,
                            (unsigned char *)segs[i].start, segs[i].len);
      if (child == raxNotFound) {
        route_node_t *new_child = route_node_create();
        if (!new_child) {
          free(segs);
          free(pattern_buf);
          return -1;
        }
        void *old = NULL;
        if (!raxInsert(node->children,
                       (unsigned char *)segs[i].start, segs[i].len,
                       new_child, &old)
            && !old) {
          route_node_free(new_child);
          free(segs);
          free(pattern_buf);
          return -1;
        }
        node = new_child;
      } else {
        node = (route_node_t *)child;
      }
    }
  }

  char **names = NULL;
  if (collected_count > 0) {
    names = malloc(sizeof(char *) * collected_count);
    if (!names) {
      free(segs);
      free(pattern_buf);
      return -1;
    }
    for (uint8_t j = 0; j < collected_count; j++) {
      names[j] = malloc(collected_name_lens[j] + 1);
      if (!names[j]) {
        for (uint8_t k = 0; k < j; k++)
          free(names[k]);
        free(names);
        free(segs);
        free(pattern_buf);
        return -1;
      }
      memcpy(names[j], collected_names[j], collected_name_lens[j]);
      names[j][collected_name_lens[j]] = '\0';
    }
  }

  if (node->route_param_names[method_idx]) {
    for (uint8_t j = 0; j < node->route_param_count[method_idx]; j++)
      free(node->route_param_names[method_idx][j]);
    free(node->route_param_names[method_idx]);
  }
  node->handlers[method_idx] = handler;
  node->middleware_ctx[method_idx] = middleware_ctx;
  node->route_param_names[method_idx] = names;
  node->route_param_count[method_idx] = collected_count;

  free(segs);
  free(pattern_buf);
  table->route_count++;
  return 0;
}

bool route_table_match(route_table_t *table,
                       llhttp_t *parser,
                       const tokenized_path_t *tokenized_path,
                       route_match_t *match,
                       ecewo_arena_t *arena) {
  if (!table || !parser || !tokenized_path || !match)
    return false;

  llhttp_method_t method = llhttp_get_method(parser);
  int method_idx = method_to_index(method);

  if (method_idx < 0)
    return false;

  match->handler = NULL;
  match->middleware_ctx = NULL;
  match->param_count = 0;
  match->params = NULL;
  match->param_capacity = MAX_INLINE_PARAMS;

  if (!table->root)
    return false;

  route_node_t *leaf = NULL;

  // Pass 1: endpoints only (no wildcards): ensures param endpoint matches
  // always beat wildcard-zero matches at a different tree branch.
  if (!match_node(table->root, tokenized_path, 0, method_idx, match, arena, false, &leaf)) {
    // Pass 2: allow wildcard fallback
    if (!match_node(table->root, tokenized_path, 0, method_idx, match, arena, true, &leaf))
      return false;
  }

  // Fill in the deferred param key names from the winning leaf's name table.
  if (leaf && match->param_count > 0) {
    bool is_wildcard = (match->handler == leaf->wildcard_handlers[method_idx]);
    char **names = is_wildcard
        ? leaf->wildcard_param_names[method_idx]
        : leaf->route_param_names[method_idx];
    if (names) {
      param_match_t *arr = match->params ? match->params : match->inline_params;
      for (uint8_t i = 0; i < match->param_count; i++) {
        arr[i].key.data = names[i];
        arr[i].key.len = strlen(names[i]);
      }
    }
  }

  return true;
}

void route_table_free(route_table_t *table) {
  if (!table)
    return;

  route_node_free(table->root);
  free(table);
}
