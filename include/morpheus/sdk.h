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

typedef struct morph_image_service morph_image_service;
typedef unsigned long morph_image_id;

typedef enum morph_image_status {
    MORPH_IMAGE_PENDING = 0,
    MORPH_IMAGE_READY = 1,
    MORPH_IMAGE_FAILED = 2
} morph_image_status;

typedef struct morph_image_result {
    morph_image_status status;
    unsigned int width;
    unsigned int height;
    const char *error;
} morph_image_result;

/* Encoded image bytes are copied before this call returns. */
morph_image_id morph_image_load_memory(
    morph_image_service *service,
    const void *data,
    unsigned long size);
/* URL loading is asynchronous and uses the host's bounded HTTP service. */
morph_image_id morph_image_load_url(
    morph_image_service *service,
    const char *url);
int morph_image_poll(
    morph_image_service *service,
    morph_image_id image_id,
    morph_image_result *result);
/* Draw into the current Nuklear layout cell. Returns zero unless ready. */
int morph_image_draw(
    morph_image_service *service,
    morph_image_id image_id);
void morph_image_release(
    morph_image_service *service,
    morph_image_id image_id);

typedef struct morph_json_document morph_json_document;
typedef struct morph_json_value morph_json_value;
typedef struct morph_json_builder morph_json_builder;
typedef struct morph_json_mut_value morph_json_mut_value;

typedef enum morph_json_type {
    MORPH_JSON_INVALID = 0,
    MORPH_JSON_NULL = 1,
    MORPH_JSON_BOOLEAN = 2,
    MORPH_JSON_INTEGER = 3,
    MORPH_JSON_NUMBER = 4,
    MORPH_JSON_STRING = 5,
    MORPH_JSON_ARRAY = 6,
    MORPH_JSON_OBJECT = 7
} morph_json_type;

typedef struct morph_json_error {
    unsigned long position;
    unsigned int code;
    char message[128];
} morph_json_error;

typedef struct morph_json_buffer {
    char *data;
    unsigned long size;
} morph_json_buffer;

/* Parsed values are borrowed and remain valid until their document is freed. */
morph_json_document *morph_json_parse(
    const char *json,
    unsigned long size,
    morph_json_error *error);
void morph_json_document_free(morph_json_document *document);
const morph_json_value *morph_json_root(const morph_json_document *document);
morph_json_type morph_json_value_type(const morph_json_value *value);
const morph_json_value *morph_json_object_get(
    const morph_json_value *object,
    const char *key);
unsigned long morph_json_array_size(const morph_json_value *array);
const morph_json_value *morph_json_array_get(
    const morph_json_value *array,
    unsigned long index);
int morph_json_get_boolean(const morph_json_value *value, int *output);
int morph_json_get_integer(const morph_json_value *value, long long *output);
int morph_json_get_number(const morph_json_value *value, double *output);
const char *morph_json_get_string(
    const morph_json_value *value,
    unsigned long *size);

/* Mutable values are borrowed until their builder is freed and attach once. */
morph_json_builder *morph_json_builder_create(void);
void morph_json_builder_free(morph_json_builder *builder);
morph_json_mut_value *morph_json_make_null(morph_json_builder *builder);
morph_json_mut_value *morph_json_make_boolean(
    morph_json_builder *builder,
    int value);
morph_json_mut_value *morph_json_make_integer(
    morph_json_builder *builder,
    long long value);
morph_json_mut_value *morph_json_make_number(
    morph_json_builder *builder,
    double value);
morph_json_mut_value *morph_json_make_string(
    morph_json_builder *builder,
    const char *value,
    unsigned long size);
morph_json_mut_value *morph_json_make_array(morph_json_builder *builder);
morph_json_mut_value *morph_json_make_object(morph_json_builder *builder);
int morph_json_array_append(
    morph_json_builder *builder,
    morph_json_mut_value *array,
    morph_json_mut_value *value);
int morph_json_object_set(
    morph_json_builder *builder,
    morph_json_mut_value *object,
    const char *key,
    morph_json_mut_value *value);
int morph_json_builder_set_root(
    morph_json_builder *builder,
    morph_json_mut_value *root);
int morph_json_serialize(
    morph_json_builder *builder,
    morph_json_buffer *output);
void morph_json_buffer_free(morph_json_buffer *buffer);

#endif
