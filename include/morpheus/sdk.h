#ifndef MORPHEUS_SDK_H
#define MORPHEUS_SDK_H

#include <stddef.h>

typedef struct morph_http_service morph_http_service;
typedef unsigned long morph_http_request_id;

typedef struct morph_http_result {
    int completed;
    int failed;
    long status_code;
    const char *body;
    unsigned long body_size;
    const char *error;
} morph_http_result;

/* Start a request. The returned body remains valid until cancellation or reuse. */
morph_http_request_id morph_http_get(
    morph_http_service *service,
    const char *url);
morph_http_request_id morph_http_post_json(
    morph_http_service *service,
    const char *url,
    const char *json,
    unsigned long json_size);
/* Results remain valid until morph_http_cancel or service shutdown. */
int morph_http_poll(
    morph_http_service *service,
    morph_http_request_id request_id,
    morph_http_result *result);
void morph_http_cancel(
    morph_http_service *service,
    morph_http_request_id request_id);

#endif
