# Using CBOR

It is possible to send `CBOR` responses in ecewo. Let's make an `add_user` and `get_all_users` example that we've already done with [cJSON](https://github.com/DaveGamble/cJSON) and [SQLite3](https://sqlite.org/) in [Using SQLite](/docs/examples/using_sqlite.md) chapter, but we'll implement with [TinyCBOR](https://github.com/intel/tinycbor) this time.

## Installation

Add this code block into your `CMakeLists.txt` file:

```cmake
FetchContent_Declare(
  tinycbor
  GIT_REPOSITORY https://github.com/intel/tinycbor.git
  GIT_TAG main
)

FetchContent_MakeAvailable(tinycbor)

target_link_libraries(server PRIVATE tinycbor)
```

And rebuild.

## Usage

### Encoding CBOR

We did this with JSON in [this example](/examples/using-json#creating-json). Now let's do it again with CBOR this time.

```c
// main.c

#include "ecewo.h"
#include "cbor.h"
#include <stdio.h>

void hello_world_cbor(Req *req, Res *res) {
    uint8_t buffer[128]; // Temporary buffer for CBOR output
    // Normally it should be allocated dynamically

    CborEncoder encoder, mapEncoder;

    // Initialize the CBOR encoder with the buffer
    cbor_encoder_init(&encoder, buffer, sizeof(buffer), 0);

    // Create a CBOR map object: { "hello": "world" }
    cbor_encoder_create_map(&encoder, &mapEncoder, 1); // 1 key-value pair
    cbor_encode_text_stringz(&mapEncoder, "hello");
    cbor_encode_text_stringz(&mapEncoder, "world");
    cbor_encoder_close_container(&encoder, &mapEncoder);

    // Get the length of the encoded CBOR data
    size_t len = cbor_encoder_get_buffer_size(&encoder, buffer);

    // Send the CBOR response with status code 200
    ecewo_set_header(res, "Content-Type", "application/cbor");
    ecewo_reply(res, 200, buffer, len);
}

int main(void) {
    App *app = ecewo_create();
    if (!app) {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    ECEWO_GET(app, "/cbor", hello_world_cbor);

    if (ecewo_listen(app, 3000) != 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    ecewo_run(app);
    return 0;
}
```

### Decoding CBOR

You can see [the exact example](/examples/using-json/#parsing-json) with JSON.

```c
// main.c

#include "ecewo.h"
#include "cbor.h"
#include <stdio.h>

void handle_user_cbor(Req *req, Res *res) {
    const uint8_t *data = (const uint8_t *)req->body;
    size_t data_len = req->body_len; // Length in bytes of the incoming body

    if (!data || data_len == 0) {
        ecewo_send_text(res, 400, "Missing request body");
        return;
    }

    // Initialize the CBOR parser
    CborParser parser;
    CborValue it;
    CborError err = cbor_parser_init(data, data_len, 0, &parser, &it);
    if (err != CborNoError) {
        ecewo_send_text(res, 400, "Invalid CBOR");
        return;
    }

    // Check that the outermost item is a map (dictionary)
    if (!cbor_value_is_map(&it)) {
        ecewo_send_text(res, 400, "Expected CBOR map");
        return;
    }

    // Extract string fields from the map
    CborValue map = it;
    CborValue val;
    char *name = NULL, *surname = NULL, *username = NULL;
    size_t len;

    // name
    err = cbor_value_map_find_value(&map, "name", &val);
    if (err != CborNoError || !cbor_value_is_text_string(&val)) {
        ecewo_send_text(res, 400, "Missing or invalid 'name'");
        goto cleanup;
    }
    err = cbor_value_dup_text_string(&val, &name, &len, &it);
    if (err != CborNoError) {
        ecewo_send_text(res, 400, "Failed to read 'name'");
        goto cleanup;
    }

    // surname
    err = cbor_value_map_find_value(&map, "surname", &val);
    if (err != CborNoError || !cbor_value_is_text_string(&val)) {
        ecewo_send_text(res, 400, "Missing or invalid 'surname'");
        goto cleanup;
    }
    err = cbor_value_dup_text_string(&val, &surname, &len, &it);
    if (err != CborNoError) {
        ecewo_send_text(res, 400, "Failed to read 'surname'");
        goto cleanup;
    }

    // username
    err = cbor_value_map_find_value(&map, "username", &val);
    if (err != CborNoError || !cbor_value_is_text_string(&val)) {
        ecewo_send_text(res, 400, "Missing or invalid 'username'");
        goto cleanup;
    }
    err = cbor_value_dup_text_string(&val, &username, &len, &it);
    if (err != CborNoError) {
        ecewo_send_text(res, 400, "Failed to read 'username'");
        goto cleanup;
    }

    // If successful
    printf("Name: %s\n", name);
    printf("Surname: %s\n", surname);
    printf("Username: %s\n", username);
    ecewo_send_text(res, 200, "Success!");

cleanup:
    // Free allocated strings
    if (name)
        free(name);
    if (surname)
        free(surname);
    if (username)
        free(username);
}

int main(void) {
    App *app = ecewo_create();
    if (!app) {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    ECEWO_POST(app, "/cbor", handle_user_cbor);

    if (ecewo_listen(app, 3000) != 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    ecewo_run(app);
    return 0;
}
```

## Test

Now ne need to test our CBOR handler. We will use Python for testing, but you can use any tool you want.

### Test Encoding CBOR

Let's run our server and download a `.cbor` binary.

```
ecewo run
```

Let's send a `curl` request and download the binary:
```
curl -X GET http://localhost:3000/cbor -o hello_world.cbor
```

This command will generate a `hello_world.cbor` binary in the root directory.

Now we need to read the inside of it. Cut that generated binary file, create a new folder outside of our project and paste it there. The new folder is for testing our CBOR binaries and they shouldn't be in the same directory with our ecewo project.

Run `pip install cbor2` and create a Python file:

```py
// testing/hello_world.py

import cbor2

with open("hello_world.cbor", "rb") as f:
    data = cbor2.load(f)

print(data)
```

Now if we run `python hello_world.py` command in the terminal, we'll see this response in the terminal:

```
{'hello': 'world'}
```

### Testing Decoding CBOR

This time we'll create a `.cbor` binary using Python and send it to our server via POSTMAN. Let's create a new `.py` file:

```py
// testing/handle_user.py

import cbor2

# Test data
data = {
    "name": "John",
    "surname": "Doe",
    "username": "johndoe"
}

# Convert to CBOR and write to file
with open("handle_user.cbor", "wb") as f:
    f.write(cbor2.dumps(data))

print("handle_user.cbor created.")
```

Run:
```
python handle_user.py
```

There will be generated a `handle_user.cbor` binary in the root directory. Now it's time to send that binary file to our server via POSTMAN.

Open the POSTMAN and set a `POST` request to the `http://localhost:3000/cbor` address. Select the `binary` body and send the `handle_user.cbor` file that we generated.

When we send the request, we'll see a `Success!` message if the process completed successfully. And in the terminal of ecewo, we'll see the content of our binary:

```
Name: John
Surname: Doe
Username: johndoe
```
