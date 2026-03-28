# Environment Variables

First, we need to add [dotenv-c](https://github.com/Isty001/dotenv-c) to our project, and then create a `.env` file in the root directory of our project.

## Installation

Create a `.env` file in the root directory. Then, copy the [dotenv.c](https://github.com/Isty001/dotenv-c/blob/master/src/dotenv.c) and [dotenv.h](https://github.com/Isty001/dotenv-c/blob/master/src/dotenv.h) files from the repository and paste them into `vendors/` folder. Make sure `dotenv.c` is part of the CMake build configuration.

## Usage

Project structure should something be like this:

```
your-project/
├── CMakeLists.txt
├── .env
├── vendors/
│   ├── dotenv.c
│   └── dotenv.h
└── src/
    └── main.c
```

We define a `PORT` in `.env`:

```
PORT=3000
```

Let's parse it in `main.c`:

```c
// src/main.c

#include "ecewo.h"
#include "dotenv.h"
#include <stdio.h>

int main(void) {
    App *app = ecewo_create();
    if (!app) {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    env_load("..", false);              // Load ".env" file
    const char *port = getenv("PORT");  // Get the "PORT"
    printf("PORT: %s\n", port);         // Print the "PORT"

    // All the variables
    // that we receive from .env file
    // will be const char*
    // so we need type casting
    // to use it as PORT
    const int PORT = (int)atoi(port);

    ecewo_listen(app, PORT);
    return 0;
}
```
