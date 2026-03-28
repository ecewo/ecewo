<div align="center">
  <img src="https://raw.githubusercontent.com/ecewo/ecewo/main/img/ecewo.svg" />
  <h1>Express-C Effect for Web Operations</h1>
  A web framework for C — inspired by <a href="https://expressjs.com">express.js</a>
</div>

## Table of Contents

- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Dependencies](#dependencies)
- [Running Tests](#running-tests)
- [Documentation](#documentation)
- [Plugins](#plugins)
- [Future Features](#future-features)
- [Example App](#example-app)
- [Contributing](#contributing)
- [License](#license)

---

## Requirements

- A C compiler (GCC or Clang)
- CMake version 3.14 or higher

---

## Quick Start

**main.c:**
```c
#include "ecewo.h"
#include <stdio.h>

void hello_world(Req *req, Res *res) {
  send_text(res, OK, "Hello, World!");
}

int main(void) {
  App *app = ecewo();
  if (!app) {
    fprintf(stderr, "Failed to initialize server\n");
    return -1;
  }

  get(app, "/", hello_world);

  if (server_listen(app, 3000) != 0) {
    fprintf(stderr, "Failed to start server\n");
    return -1;
  }

  server_run(app);
  return 0;
}
```

**CMakeLists.txt:**
```sh
cmake_minimum_required(VERSION 3.14)
project(app VERSION 1.0.0 LANGUAGES C)

include(FetchContent)

FetchContent_Declare(
  ecewo
  GIT_REPOSITORY https://github.com/ecewo/ecewo.git
  GIT_TAG v4.0.0
)

FetchContent_MakeAvailable(ecewo)

add_executable(${PROJECT_NAME}
  main.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE ecewo)
```

**Build and Run:**

```shell
mkdir build
cd build
cmake ..
cmake --build .
./app
```

---

## Dependencies

- [libuv](https://github.com/libuv/libuv) for async event loop.
- [llhttp](https://github.com/nodejs/llhttp) for HTTP parsing.
- [rax](https://github.com/antirez/rax) for radix-tree router.
- A customized arena allocator based on [tsoding/arena](https://github.com/tsoding/arena).

No manual installation required for any of these dependencies.

---

## Running Tests

```shell
mkdir build
cd build
cmake -DECEWO_BUILD_TESTS=ON ..
cmake --build .
ctest
```

---

## Documentation

Refer to the [docs](/docs/) for usage.

---

## Plugins

- [`ecewo-cluster`](https://github.com/ecewo/ecewo-cluster) for multithreading.
- [`ecewo-cookie`](https://github.com/ecewo/ecewo-cookie) for cookie management.
- [`ecewo-cors`](https://github.com/ecewo/ecewo-cors) for CORS impelentation.
- [`ecewo-fs`](https://github.com/ecewo/ecewo-fs) for file operations.
- [`ecewo-helmet`](https://github.com/ecewo/ecewo-helmet) for automatically setting safety headers.
- [`ecewo-mock`](https://github.com/ecewo/ecewo-mock) for mocking requests.
- [`ecewo-postgres`](https://github.com/ecewo/ecewo-postgres) for async PostgreSQL integration.
- [`ecewo-session`](https://github.com/ecewo/ecewo-session) for session management.
- [`ecewo-static`](https://github.com/ecewo/ecewo-static) for static file serving.

---

## Future Features

I'm not giving my word, but I'm planning to add these features in the future:

- Rate limiter
- WebSocket
- TLS
- SSE
- HTTP/2
- C++ and Nim bindings
- Redis plugin

---

## Example App

[Here](https://github.com/ecewo/ecewo-example) is an example blog app built with ecewo and PostgreSQL.

---

## Contributing

Contributions are welcome. Please feel free to submit pull requests or open issues. See the [CONTRIBUTING.md](/CONTRIBUTING.md).

---

## License

Licensed under [MIT](./LICENSE).
