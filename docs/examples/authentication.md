# Authentication

## Table of Contents

1. [Login](#login)
2. [Logout](#logout)

## Login

```c
#include "ecewo.h"
#include "ecewo/session.h"
#include "cJSON.h"

// Example user info (it must be saved in a database)
const char *STATIC_NAME = "John Doe";
const char *STATIC_USERNAME = "johndoe";
const char *STATIC_PASSWORD = "123123";
const char *STATIC_USER_ID = "1";

void handle_login(ecewo_request_t *req, ecewo_response_t *res) {
   const char *body = req->body;
   if (!body)
   {
      ecewo_send_text(res, 400, "Missing request body");
      return;
   }

   cJSON *json = cJSON_Parse(body);
   if (!json)
   {
      ecewo_send_text(res, 400, "Invalid JSON");
      return;
   }

   const char *username = cJSON_GetObjectItem(json, "username")->valuestring;
   const char *password = cJSON_GetObjectItem(json, "password")->valuestring;

   if (!username || !password)
   {
      cJSON_Delete(json);
      ecewo_send_text(res, 400, "Missing fields");
      return;
   }

   if (strcmp(username, STATIC_USERNAME) == 0 && strcmp(password, STATIC_PASSWORD) == 0)
   {
      // Create session on RAM for 1 hour
      Session *session = session_create(3600);

      // Set key-value variables to the session
      session_set(session, "name", STATIC_NAME);
      session_set(session, "username", STATIC_USERNAME);
      session_set(session, "id", STATIC_USER_ID);
      session_set(session, "theme", "dark");

      /*
      Send the session as cookie for 1 hour
      max_age is not required because
      it will be calculated from session expiration
      */
      Cookie cookie_options = {
          .path = "/",
          .same_site = "Lax",
          .http_only = true,
          .secure = true,
      };

      session_send(res, session, &cookie_options);

      printf("Session ID: %s\n", session->id);
      ecewo_send_text(res, 200, "Login successful!");
   }
   else
   {
      ecewo_send_text(res, 401, "Invalid username or password");
   }

   cJSON_Delete(json);
   // Free the created session too if you will save it in a database:
   // session_free(session);
}

void cleanup_app(void) {
    session_cleanup();
}

int main(void) {
    ecewo_app_t *app = ecewo_create();
    session_init();

    ECEWO_POST(app, "/login", handle_login);

    ecewo_atexit(app, cleanup_app);
    ecewo_listen(app, 3000);
    return 0;
}
```

Let's send a request to http://localhost:3000/login with that body:

```json
{
    "username": "johndoe",
    "password": "123123"
}
```

If login is successful, we'll see a `Login successful!` response and a header like `"Cookie": "session=VKdbMRbqMhh_40F6ef2FreEba6JqkH16"` will be added to the headers.

## Logout

```c
// ...
// [handle_login is here]
// ...

// Add this handler:

void handle_logout(ecewo_request_t *req, ecewo_response_t *res) {
    // First, check if the user has session
    Session *session = session_get(req);

    if (!session) {
        ecewo_send_text(res, 400, "You have to login first");
    }
    else
    {
        // The Cookie options should be the same
        // as what we use in the login handler
        Cookie cookie_options = {
            .path = "/",
            .same_site = "Lax",
            .http_only = true,
            .secure = true,
        };

        session_destroy(res, session, &cookie_options);
        ecewo_send_text(res, 200, "Logged out");
    }
}

void cleanup_app(void) {
    session_cleanup();
}

int main(void) {
    ecewo_app_t *app = ecewo_create();
    session_init();

    ECEWO_POST(app, "/login", handle_login);
    ECEWO_GET(app, "/logout", handle_logout); // We also added it

    ecewo_atexit(app, cleanup_app);
    ecewo_listen(app, 3000);
    return 0;
}
```

`session_destroy()` is destroying the session from the memory and deleting the cookie from the client.

Now let's send a request to `http://localhost:3000/logout` after login. Cookie header will be deleted and we'll see that response:

```
Logged out
```

If we send one more request, we'll see:

```
You have to login first
```

> [!INFO]
>
> `session_get()` is running `cookie_get()` under the hood, but it's specialized to extract the session from the `Cookie` header. While you need to manually free the memory returned by `cookie_get()`, you don't need to do that with `session_get()` — it handles memory management internally.

### Getting Session Data

We added 3 data to the session in the Login handler: name, username and theme. Let's write another function that sends the session data:

```c
// ...
// [handle_login and handle_logout is here]
// ...

void handle_session_data(ecewo_request_t *req, ecewo_response_t *res) {
   Session *session = session_get(req);

   if (!session)
   {
      ecewo_send_text(res, 401, "Error: Authentication required");
      return;
   }

   char *name = session_value_get(session, "name");
   if (name)
   {
      printf("Name: %s\n", name);
      free(name); // Free the memory!
   }

   char *username = session_value_get(session, "username");
   if (username)
   {
      printf("Username: %s\n", username);
      free(username); // Free the memory!
   }

   char *id = session_value_get(session, "id");
   if (id)
   {
      printf("ID: %s\n", id);
      free(id); // Free the memory!
   }

   char *theme = session_value_get(session, "theme");
   if (theme)
   {
      printf("Theme: %s\n", theme);
      free(theme); // Free the memory!
   }

   ecewo_send_text(res, OK, "Check out the console!");
}


void cleanup_app(void) {
    session_cleanup();
}

int main(void) {
    ecewo_app_t *app = ecewo_create();
    session_init();

    ECEWO_GET(app, "/session", handle_session_data); // We added it now
    ECEWO_POST(app, "/login", handle_login);
    ECEWO_POST(app, "/logout", handle_logout);

    ecewo_atexit(app, cleanup_app);
    ecewo_listen(app, 3000);
    return 0;
}
```

First, we need to login. Rebuild the program and send a `POST` request to the `http://localhost:3000/login` and get the session. After that, send another request to the `http://localhost:3000/session` address to see the session data.

We'll get a `Check out the console!` response, and the output in the console will this:

```
Name: John Doe
Username: johndoe
ID: 1
Theme: dark
```

Here are the session data, which we have added while the user is logging in.

### Protected Routes

Let's say that we want some pages to be available for authenticated users only. In this situation, we can use get_session() and get_session_value() functions to check if the user has a session.

```c
// <-- Here are the other handlers -->

void handle_protected_route(ecewo_request_t *req, ecewo_response_t *res) {
   // Get session from request
   Session *sess = session_get(req);
   if (!sess)
   {
      // No session, user not logged in
      ecewo_send_text(res, UNAUTHORIZED, "Error: Authentication required");
      return;
   }

   // Get user information
   char *user_id = session_value_get(sess, "id");

   if (user_id && strcmp(user_id, STATIC_USER_ID) == 0)
   {
      char *welcome_message = ecewo_sprintf(res->arena, "Welcome, %s", STATIC_NAME);
      ecewo_send_text(res, OK, welcome_message);
   }
   else
   {
      ecewo_send_text(res, FORBIDDEN, "You are not allowed to see this page.");
   }

   free(user_id);
}

void cleanup_app(void) {
    session_cleanup();
}

int main(void) {
    ecewo_app_t *app = ecewo_create();
    session_init();

    ECEWO_GET(app, "/protected", handle_protected_route); // We added it now
    ECEWO_GET(app, "/session", handle_session_data);
    ECEWO_POST(app, "/login", handle_login);
    ECEWO_POST(app, "/logout", handle_logout);

    ecewo_atexit(app, cleanup_app);
    ecewo_listen(app, 3000);
    return 0;
}
```

Let's send a request to `http://localhost:3000/protected`. If we authenticated as John Doe, we'll see:

```
Welcome to the protected area, John Doe
```

If we are logged in, but not as John Doe, we'll see:

```
You must be logged in.
```

If we did not log in at all, we'll see:

```
Error: Authentication required
```

> [!NOTE]
>
> It's not safe to insert the password to the database without encryption. You should use a library to encrypt the user password before inserting.

> [!NOTE]
>
> In these examples, session is stored in memory, but you can store them in the database if you prefer.
>
> If you store them in the memory, you will use `session_free()` function for rare operations like logout. ecewo will free the expired sessions when a new session is created.
>
> But if you prefer storing the sessions in a database, you may free the session from memory right after you create and insert it into the database.
