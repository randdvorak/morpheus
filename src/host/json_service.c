#include "morpheus/sdk.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yyjson.h"

struct morph_json_document {
    yyjson_doc *document;
};

struct morph_json_builder {
    yyjson_mut_doc *document;
};

static void morph_json_clear_error(morph_json_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
}

static void morph_json_set_error(
    morph_json_error *error,
    const yyjson_read_err *source)
{
    if (!error || !source) return;
    error->position = (unsigned long)source->pos;
    error->code = (unsigned int)source->code;
    if (source->msg) {
        snprintf(error->message, sizeof(error->message), "%s", source->msg);
    }
}

morph_json_document *morph_json_parse(
    const char *json,
    unsigned long size,
    morph_json_error *error)
{
    yyjson_read_err read_error;
    morph_json_document *wrapper;
    yyjson_doc *document;

    morph_json_clear_error(error);
    if (!json && size) return NULL;
    memset(&read_error, 0, sizeof(read_error));
    document = yyjson_read_opts(
        (char *)(json ? json : ""),
        (size_t)size,
        0,
        NULL,
        &read_error);
    if (!document) {
        morph_json_set_error(error, &read_error);
        return NULL;
    }
    wrapper = (morph_json_document *)malloc(sizeof(*wrapper));
    if (!wrapper) {
        yyjson_doc_free(document);
        if (error) snprintf(error->message, sizeof(error->message), "Out of memory");
        return NULL;
    }
    wrapper->document = document;
    return wrapper;
}

void morph_json_document_free(morph_json_document *document)
{
    if (!document) return;
    yyjson_doc_free(document->document);
    free(document);
}

const morph_json_value *morph_json_root(const morph_json_document *document)
{
    return document
        ? (const morph_json_value *)yyjson_doc_get_root(document->document)
        : NULL;
}

morph_json_type morph_json_value_type(const morph_json_value *value)
{
    yyjson_val *raw = (yyjson_val *)value;
    if (!raw) return MORPH_JSON_INVALID;
    if (yyjson_is_null(raw)) return MORPH_JSON_NULL;
    if (yyjson_is_bool(raw)) return MORPH_JSON_BOOLEAN;
    if (yyjson_is_int(raw)) return MORPH_JSON_INTEGER;
    if (yyjson_is_num(raw)) return MORPH_JSON_NUMBER;
    if (yyjson_is_str(raw)) return MORPH_JSON_STRING;
    if (yyjson_is_arr(raw)) return MORPH_JSON_ARRAY;
    if (yyjson_is_obj(raw)) return MORPH_JSON_OBJECT;
    return MORPH_JSON_INVALID;
}

const morph_json_value *morph_json_object_get(
    const morph_json_value *object,
    const char *key)
{
    yyjson_val *raw = (yyjson_val *)object;
    if (!raw || !key || !yyjson_is_obj(raw)) return NULL;
    return (const morph_json_value *)yyjson_obj_get(raw, key);
}

unsigned long morph_json_array_size(const morph_json_value *array)
{
    yyjson_val *raw = (yyjson_val *)array;
    return raw && yyjson_is_arr(raw) ? (unsigned long)yyjson_arr_size(raw) : 0;
}

const morph_json_value *morph_json_array_get(
    const morph_json_value *array,
    unsigned long index)
{
    yyjson_val *raw = (yyjson_val *)array;
    if (!raw || !yyjson_is_arr(raw) || index >= (unsigned long)yyjson_arr_size(raw)) {
        return NULL;
    }
    return (const morph_json_value *)yyjson_arr_get(raw, (size_t)index);
}

int morph_json_get_boolean(const morph_json_value *value, int *output)
{
    yyjson_val *raw = (yyjson_val *)value;
    if (!raw || !output || !yyjson_is_bool(raw)) return 0;
    *output = yyjson_get_bool(raw) ? 1 : 0;
    return 1;
}

int morph_json_get_integer(const morph_json_value *value, long long *output)
{
    yyjson_val *raw = (yyjson_val *)value;
    if (!raw || !output || !yyjson_is_int(raw)) return 0;
    if (yyjson_is_sint(raw)) {
        *output = (long long)yyjson_get_sint(raw);
    } else {
        uint64_t integer = yyjson_get_uint(raw);
        if (integer > (uint64_t)LLONG_MAX) return 0;
        *output = (long long)integer;
    }
    return 1;
}

int morph_json_get_number(const morph_json_value *value, double *output)
{
    yyjson_val *raw = (yyjson_val *)value;
    if (!raw || !output || !yyjson_is_num(raw)) return 0;
    *output = yyjson_get_num(raw);
    return 1;
}

const char *morph_json_get_string(
    const morph_json_value *value,
    unsigned long *size)
{
    yyjson_val *raw = (yyjson_val *)value;
    if (size) *size = 0;
    if (!raw || !yyjson_is_str(raw)) return NULL;
    if (size) *size = (unsigned long)yyjson_get_len(raw);
    return yyjson_get_str(raw);
}

morph_json_builder *morph_json_builder_create(void)
{
    morph_json_builder *builder = (morph_json_builder *)malloc(sizeof(*builder));
    if (!builder) return NULL;
    builder->document = yyjson_mut_doc_new(NULL);
    if (!builder->document) {
        free(builder);
        return NULL;
    }
    return builder;
}

void morph_json_builder_free(morph_json_builder *builder)
{
    if (!builder) return;
    yyjson_mut_doc_free(builder->document);
    free(builder);
}

morph_json_mut_value *morph_json_make_null(morph_json_builder *builder)
{
    return builder
        ? (morph_json_mut_value *)yyjson_mut_null(builder->document)
        : NULL;
}

morph_json_mut_value *morph_json_make_boolean(morph_json_builder *builder, int value)
{
    return builder
        ? (morph_json_mut_value *)yyjson_mut_bool(builder->document, value != 0)
        : NULL;
}

morph_json_mut_value *morph_json_make_integer(
    morph_json_builder *builder,
    long long value)
{
    return builder
        ? (morph_json_mut_value *)yyjson_mut_sint(builder->document, (int64_t)value)
        : NULL;
}

morph_json_mut_value *morph_json_make_number(morph_json_builder *builder, double value)
{
    return builder
        ? (morph_json_mut_value *)yyjson_mut_real(builder->document, value)
        : NULL;
}

morph_json_mut_value *morph_json_make_string(
    morph_json_builder *builder,
    const char *value,
    unsigned long size)
{
    if (!builder || (!value && size)) return NULL;
    return (morph_json_mut_value *)yyjson_mut_strncpy(
        builder->document,
        value ? value : "",
        (size_t)size);
}

morph_json_mut_value *morph_json_make_array(morph_json_builder *builder)
{
    return builder
        ? (morph_json_mut_value *)yyjson_mut_arr(builder->document)
        : NULL;
}

morph_json_mut_value *morph_json_make_object(morph_json_builder *builder)
{
    return builder
        ? (morph_json_mut_value *)yyjson_mut_obj(builder->document)
        : NULL;
}

int morph_json_array_append(
    morph_json_builder *builder,
    morph_json_mut_value *array,
    morph_json_mut_value *value)
{
    if (!builder || !array || !value) return 0;
    return yyjson_mut_arr_append(
        (yyjson_mut_val *)array,
        (yyjson_mut_val *)value) ? 1 : 0;
}

int morph_json_object_set(
    morph_json_builder *builder,
    morph_json_mut_value *object,
    const char *key,
    morph_json_mut_value *value)
{
    yyjson_mut_val *key_value;
    if (!builder || !object || !key || !value) return 0;
    key_value = yyjson_mut_strcpy(builder->document, key);
    if (!key_value) return 0;
    return yyjson_mut_obj_add(
        (yyjson_mut_val *)object,
        key_value,
        (yyjson_mut_val *)value) ? 1 : 0;
}

int morph_json_builder_set_root(
    morph_json_builder *builder,
    morph_json_mut_value *root)
{
    if (!builder || !root) return 0;
    yyjson_mut_doc_set_root(builder->document, (yyjson_mut_val *)root);
    return 1;
}

int morph_json_serialize(morph_json_builder *builder, morph_json_buffer *output)
{
    size_t size = 0;
    char *data;
    if (!builder || !output || !yyjson_mut_doc_get_root(builder->document)) return 0;
    output->data = NULL;
    output->size = 0;
    data = yyjson_mut_write(builder->document, 0, &size);
    if (!data) return 0;
    output->data = data;
    output->size = (unsigned long)size;
    return 1;
}

void morph_json_buffer_free(morph_json_buffer *buffer)
{
    if (!buffer) return;
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}
