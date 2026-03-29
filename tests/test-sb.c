// MIT License

// Copyright (c) 2026 Savas Sahin <savashn@proton.me>

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

#include "arena.h"
#include "tester.h"
#include <stdint.h>
#include <string.h>

// Null-only produces a single NUL byte
int test_sb_null_only(void) {
  Arena a = {0};
  StringBuilder sb = {0};

  ecewo_sb_append_null(&a, &sb);

  ASSERT_EQ(1, (int64_t)sb.count);
  ASSERT_EQ('\0', sb.items[0]);

  ecewo_free(&a);
  RETURN_OK();
}


// Single string append + null
int test_sb_single_append(void) {
  Arena a = {0};
  StringBuilder sb = {0};

  ecewo_sb_append_cstr(&a, &sb, "hello");
  ecewo_sb_append_null(&a, &sb);

  ASSERT_EQ(6, (int64_t)sb.count);  // 5 chars + NUL
  ASSERT_EQ_STR("hello", sb.items);

  ecewo_free(&a);
  RETURN_OK();
}


// Single char appended via ecewo_da_append
int test_sb_single_char(void) {
  Arena a = {0};
  StringBuilder sb = {0};

  ecewo_da_append(&a, &sb, 'X');
  ecewo_sb_append_null(&a, &sb);

  ASSERT_EQ(2, (int64_t)sb.count);
  ASSERT_EQ('X', sb.items[0]);
  ASSERT_EQ('\0', sb.items[1]);

  ecewo_free(&a);
  RETURN_OK();
}


// Building a slash-joined path
int test_sb_path_join(void) {
  Arena a = {0};
  StringBuilder sb = {0};

  const char *parts[] = {"api", "v1", "users"};
  for (int i = 0; i < 3; i++) {
    if (i > 0) ecewo_da_append(&a, &sb, '/');
    ecewo_sb_append_cstr(&a, &sb, parts[i]);
  }
  ecewo_sb_append_null(&a, &sb);

  ASSERT_EQ_STR("api/v1/users", sb.items);

  ecewo_free(&a);
  RETURN_OK();
}


// Many appends cross the ARENA_DA_INIT_CAP boundary
int test_sb_large_growth(void) {
  Arena a = {0};
  StringBuilder sb = {0};

  // Each iteration appends "ab" (2 bytes). 200 iterations = 400 chars,
  // which exceeds ARENA_DA_INIT_CAP=256 and forces at least one realloc.
  for (int i = 0; i < 200; i++) {
    ecewo_sb_append_cstr(&a, &sb, "ab");
  }

  ecewo_sb_append_null(&a, &sb);

  ASSERT_EQ(401, (int64_t)sb.count);  // 400 chars + NULL
  ASSERT_TRUE(sb.capacity >= 400);

  // Spot-check a few positions
  ASSERT_EQ('a', sb.items[0]);
  ASSERT_EQ('b', sb.items[1]);
  ASSERT_EQ('a', sb.items[398]);
  ASSERT_EQ('b', sb.items[399]);
  ASSERT_EQ('\0', sb.items[400]);

  ecewo_free(&a);
  RETURN_OK();
}


// Reuse after ecewo_free starts fresh
int test_sb_reuse_after_free(void) {
  Arena a = {0};
  StringBuilder sb = {0};

  ecewo_sb_append_cstr(&a, &sb, "first");
  ecewo_sb_append_null(&a, &sb);
  ASSERT_EQ_STR("first", sb.items);
  ecewo_free(&a);

  // Reset the struct and arena, build a second string
  sb = (StringBuilder){0};
  ecewo_sb_append_cstr(&a, &sb, "second");
  ecewo_sb_append_null(&a, &sb);
  ASSERT_EQ_STR("second", sb.items);

  ecewo_free(&a);
  RETURN_OK();
}

int main(void) {
  RUN_TEST(test_sb_null_only);
  RUN_TEST(test_sb_single_append);
  RUN_TEST(test_sb_single_char);
  RUN_TEST(test_sb_path_join);
  RUN_TEST(test_sb_large_growth);
  RUN_TEST(test_sb_reuse_after_free);

  return 0;
}
