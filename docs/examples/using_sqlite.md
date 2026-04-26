# Using SQLite

## Table of Contents

1. [Installation](#installation)
    1. [Example Folder Structure](#example-folder-structure)
    2. [Configure Database](#configure-database)
2. [Usage](#usage)
    1. [Inserting Data](#inserting-data)
    2. [Querying Data](#querying-data)
    3. [Initialization](#initialization)
    4. [Run](#run)

## Installation

Install SQLite3 amalgamation zip file from the [official page](https://www.sqlite.org/download.html), then add the `sqlite3.c` and `sqlite3.h` files into `vendors/` folder. Make sure `sqlite3.c` is part of the CMake build configuration.

Or, you can download [sqlite3.c](https://github.com/rhuijben/sqlite-amalgamation/blob/master/sqlite3.c) and [sqlite3.h](https://github.com/rhuijben/sqlite-amalgamation/blob/master/sqlite3.h) files directly from an unofficial [sqlite-amalgamation mirror](https://github.com/rhuijben/sqlite-amalgamation). If you prefer this, be sure it's up to date.

### Example Folder Structure

```
your-project/
├── CMakeLists.txt          # CMake of our project
├── vendors/                # Our external libraries
│   ├── sqlite3.c           # SQLite3 source file we installed
│   └── sqlite3.h           # SQLite3 header file we installed
└── src/                    # Source code of ours
    ├── handlers/           # Folder for our handlers
    │   ├── handlers.c      # Our handlers
    │   └── handlers.h      # Header file of handlers
    ├── db/                 # Folder for our database migrations
    │   ├── db.h            # Our database header file
    │   └── db.c            # Our database configs
    └── main.c              # Main application entry point
```

### Configure Database

Let's make the configuration of database:

```c
// src/db/db.c

#include "sqlite3.h"

sqlite3 *db = NULL;

// Create a user table
int create_table(void) {
    const char *create_table =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "username TEXT NOT NULL,"
        "password TEXT NOT NULL"
        ");";

    char *err_msg = NULL;

    int rc = sqlite3_exec(db, create_table, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot create the table: %s\n", err_msg);
        sqlite3_free(err_msg);
        return 1;
    }

    printf("Database and table are ready\n");
    return 0;
}

// Connection
int db_init(void) {
    int rc = sqlite3_open("sql.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open the database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Call the table function to create
    create_table();

    printf("Database connection successful\n");
    return 0;
}
```

```c
// src/db/db.h

#ifndef DB_H
#define DB_H

#include "sqlite3.h"

extern sqlite3 *db;

int db_init();

#endif
```

Now, we need to write two handlers. One for querying and one for inserting data.

## Usage

### Inserting Data

We already created a 'Users' table in the previously chapter. Now we will add a user to it. Let's begin with writing our POST handler:

```c
// src/handlers/handlers.h

#ifndef HANDLERS_H
#define HANDLERS_H

#include "ecewo.h"

void add_user(ecewo_request_t *req, ecewo_response_t *res);

#endif
```

```c
// src/handlers/handlers.c

#include "handlers.h"
#include "cJSON.h"
#include "sqlite3.h"

extern sqlite3 *db;

typedef struct {
    char *name;
    char *username;
    char *password;
    int status;
    char *message;
} AddUserContext;

// Worker function - runs in thread pool (safe to block)
void add_user_work(void *context) {
    AddUserContext *ctx = (AddUserContext *)context;
    
    const char *sql = "INSERT INTO users (name, username, password) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        ctx->status = 500;
        ctx->message = "DB prepare failed";
        return;
    }

    sqlite3_bind_text(stmt, 1, ctx->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ctx->username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, ctx->password, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        ctx->status = 500;
        ctx->message = "DB insert failed";
        return;
    }

    ctx->status = 201;
    ctx->message = "User created!";
}

// Callback function - runs on main thread
void add_user_done(ecewo_response_t *res, void *context) {
    AddUserContext *ctx = (AddUserContext *)context;
    ecewo_send_text(res, ctx->status, ctx->message);
}

// Handler function - runs on main thread
void add_user(ecewo_request_t *req, ecewo_response_t *res) {
    const char *body = req->body;

    if (body == NULL) {
        ecewo_send_text(res, 400, "Missing request body");
        return;
    }

    cJSON *json = cJSON_Parse(body);

    if (!json) {
        ecewo_send_text(res, 400, "Invalid JSON");
        return;
    }

    const char *name = cJSON_GetObjectItem(json, "name")->valuestring;
    const char *username = cJSON_GetObjectItem(json, "username")->valuestring;
    const char *password = cJSON_GetObjectItem(json, "password")->valuestring;

    if (!name || !username || !password) {
        cJSON_Delete(json);
        ecewo_send_text(res, 400, "Missing fields");
        return;
    }

    // Create context using arena
    AddUserContext *ctx = ecewo_alloc(res->arena, sizeof(AddUserContext));
    ctx->name = ecewo_strdup(res->arena, name);
    ctx->username = ecewo_strdup(res->arena, username);
    ctx->password = ecewo_strdup(res->arena, password);

    cJSON_Delete(json);

    // Spawn async work
    ecewo_spawn_http(res, ctx, add_user_work, add_user_done);
}
```

### Querying Data

Now we'll write a handler function that gives us these users' information.

```c
// src/handlers/handlers.h

#ifndef HANDLERS_H
#define HANDLERS_H

#include "ecewo.h"

void get_all_users(ecewo_request_t *req, ecewo_response_t *res); // Add this
void add_user(ecewo_request_t *req, ecewo_response_t *res);

#endif
```

```c
// src/handlers/handlers.c

#include "handlers.h"
#include "cJSON.h"
#include "sqlite3.h"

extern sqlite3 *db;

// Context for get_all_users
typedef struct {
    cJSON *json_array;
    int status;
    char *error_message;
} GetUsersContext;

// Worker function - runs in thread pool
void get_users_work(void *context) {
    GetUsersContext *ctx = (GetUsersContext *)context;
    
    const char *sql = "SELECT * FROM users;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        ctx->status = 500;
        ctx->error_message = "DB prepare failed";
        return;
    }

    ctx->json_array = cJSON_CreateArray();

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const int id = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *username = (const char *)sqlite3_column_text(stmt, 2);

        cJSON *user_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(user_json, "id", id);
        cJSON_AddStringToObject(user_json, "name", name);
        cJSON_AddStringToObject(user_json, "username", username);

        cJSON_AddItemToArray(ctx->json_array, user_json);
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        ctx->status = 500;
        ctx->error_message = "DB step failed";
        cJSON_Delete(ctx->json_array);
        ctx->json_array = NULL;
        return;
    }

    ctx->status = 200;
}

// Callback - runs on main thread
void get_users_done(ecewo_response_t *res, void *context) {
    GetUsersContext *ctx = (GetUsersContext *)context;

    if (ctx->status != 200 || !ctx->json_array) {
        ecewo_send_text(res, ctx->status, ctx->error_message);
        return;
    }

    char *json_string = cJSON_PrintUnformatted(ctx->json_array);
    ecewo_send_json(res, 200, json_string);

    cJSON_Delete(ctx->json_array);
    free(json_string);
}

// Handler - runs on main thread
void get_all_users(ecewo_request_t *req, ecewo_response_t *res) {
    GetUsersContext *ctx = ecewo_alloc(res->arena, sizeof(GetUsersContext));
    ctx->json_array = NULL;
    ctx->status = 500;
    ctx->error_message = "Unknown error";

    ecewo_spawn_http(res, ctx, get_users_work, get_users_done);
}

// ... add_user functions from previous section ...
```

### Initialization

```c
// src/main.c

#include "ecewo.h"
#include "db/db.h"
#include <stdio.h>

void destroy_app(void *user_data) {
    (void)user_data;
    sqlite3_close(db);
}

int main(void) {
    ecewo_app_t *app = ecewo_create();
    if (!app) {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    if (db_init() != 0) {
        fprintf(stderr, "Failed to initialize database\n");
        return 1;
    }

    // Register routes with ecewo_spawn() inside handlers
    ECEWO_GET(app, "/users", get_all_users);
    ECEWO_POST(app, "/user", add_user);

    ecewo_atexit(app, destroy_app, NULL);

    ecewo_listen(app, 3000);
    return 0;
}
```

### Run

Let's rebuild our server and then test:

If everything went OK, a `db.sql` file containing a `users` table will be created in your root directory. Now we can use `POSTMAN` or something else to send requests.

We'll send a `POST` request to `http://localhost:3000/user`, with the body below:

```json
{
    "name": "John Doe",
    "username": "johndoe",
    "password": "123123"
}
```

If everything is correct, the output will be `User created!`.

Let's send one more request for the next example:

```json
{
    "name": "Jane Doe",
    "username": "janedoe",
    "password": "321321"
}
```

Now, let's query the users that we just added. If we send a `GET` request to `http://localhost:3000/users`, we'll receive this output:

```json
[
  {
    "id": 1,
    "name": "John Doe",
    "username": "johndoe"
  },
  {
    "id": 2,
    "name": "Jane Doe",
    "username": "janedoe"
  }
]
```

> [!NOTE]
>
> SQLite does not have an async API, so we use `ecewo_spawn()` to run database operations in worker threads. This prevents blocking the main event loop while maintaining good performance for concurrent requests.
