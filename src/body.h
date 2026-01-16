#ifndef ECEWO_BODY_H
#define ECEWO_BODY_H

#include "ecewo.h"

// Called from http.c on_body_cb when chunk arrives
// Returns: 0 = continue, 1 = pause (backpressure), -1 = error
int body_stream_on_chunk(void *stream_ctx, const char *data, size_t len);

// Called from router.c after request fully parsed
void body_on_complete(Req *req);

// Called from router.c if parse error occurs
void body_on_error_internal(Req *req, const char *error);

#endif