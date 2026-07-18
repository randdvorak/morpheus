#include <stdio.h>
#include <string.h>

#include "morpheus/sdk.h"

int main(void)
{
    static const char input[] =
        "{\"name\":\"Morpheus\",\"revision\":7,\"ratio\":1.5,"
        "\"ready\":true,\"missing\":null,\"capabilities\":[\"http\",\"storage\"]}";
    static const char expected[] =
        "{\"status\":\"ready\",\"schema_version\":1,\"enabled\":true,"
        "\"ratio\":1.5,\"optional\":null,\"capabilities\":[\"json\",\"storage\"]}";
    morph_json_error error;
    morph_json_document *document;
    const morph_json_value *root;
    const morph_json_value *name;
    const morph_json_value *revision;
    const morph_json_value *ratio;
    const morph_json_value *ready;
    const morph_json_value *missing;
    const morph_json_value *capabilities;
    const morph_json_value *second_capability;
    unsigned long string_size = 0;
    long long integer = 0;
    double number = 0.0;
    int boolean = 0;
    morph_json_builder *builder;
    morph_json_mut_value *output_root;
    morph_json_mut_value *output_array;
    morph_json_buffer output = {0};

    document = morph_json_parse(input, sizeof(input) - 1, &error);
    if (!document) {
        fprintf(stderr, "Unable to parse valid JSON at %lu: %s\n",
            error.position,
            error.message);
        return 1;
    }
    root = morph_json_root(document);
    name = morph_json_object_get(root, "name");
    revision = morph_json_object_get(root, "revision");
    ratio = morph_json_object_get(root, "ratio");
    ready = morph_json_object_get(root, "ready");
    missing = morph_json_object_get(root, "missing");
    capabilities = morph_json_object_get(root, "capabilities");
    second_capability = morph_json_array_get(capabilities, 1);
    if (morph_json_value_type(root) != MORPH_JSON_OBJECT ||
        morph_json_value_type(name) != MORPH_JSON_STRING ||
        !morph_json_get_string(name, &string_size) || string_size != 8 ||
        strcmp(morph_json_get_string(name, NULL), "Morpheus") != 0 ||
        morph_json_value_type(revision) != MORPH_JSON_INTEGER ||
        !morph_json_get_integer(revision, &integer) || integer != 7 ||
        morph_json_value_type(ratio) != MORPH_JSON_NUMBER ||
        !morph_json_get_number(ratio, &number) || number != 1.5 ||
        !morph_json_get_boolean(ready, &boolean) || !boolean ||
        morph_json_value_type(missing) != MORPH_JSON_NULL ||
        morph_json_array_size(capabilities) != 2 ||
        !second_capability ||
        strcmp(morph_json_get_string(second_capability, NULL), "storage") != 0 ||
        morph_json_array_get(capabilities, 2) != NULL ||
        morph_json_get_integer(name, &integer)) {
        morph_json_document_free(document);
        fprintf(stderr, "Parsed facade values did not match\n");
        return 2;
    }
    morph_json_document_free(document);

    document = morph_json_parse("{\"invalid\":]", 13, &error);
    if (document || !error.message[0]) {
        morph_json_document_free(document);
        fprintf(stderr, "Invalid JSON did not produce a diagnostic\n");
        return 3;
    }

    builder = morph_json_builder_create();
    output_root = morph_json_make_object(builder);
    output_array = morph_json_make_array(builder);
    if (!builder || !output_root || !output_array ||
        !morph_json_object_set(builder, output_root, "status",
            morph_json_make_string(builder, "ready", 5)) ||
        !morph_json_object_set(builder, output_root, "schema_version",
            morph_json_make_integer(builder, 1)) ||
        !morph_json_object_set(builder, output_root, "enabled",
            morph_json_make_boolean(builder, 1)) ||
        !morph_json_object_set(builder, output_root, "ratio",
            morph_json_make_number(builder, 1.5)) ||
        !morph_json_object_set(builder, output_root, "optional",
            morph_json_make_null(builder)) ||
        !morph_json_array_append(builder, output_array,
            morph_json_make_string(builder, "json", 4)) ||
        !morph_json_array_append(builder, output_array,
            morph_json_make_string(builder, "storage", 7)) ||
        !morph_json_object_set(builder, output_root, "capabilities", output_array) ||
        !morph_json_builder_set_root(builder, output_root) ||
        !morph_json_serialize(builder, &output)) {
        morph_json_buffer_free(&output);
        morph_json_builder_free(builder);
        fprintf(stderr, "Unable to build JSON through facade\n");
        return 4;
    }
    if (output.size != sizeof(expected) - 1 || strcmp(output.data, expected) != 0) {
        fprintf(stderr, "Serialized facade JSON did not match: %s\n", output.data);
        morph_json_buffer_free(&output);
        morph_json_builder_free(builder);
        return 5;
    }
    morph_json_buffer_free(&output);
    morph_json_builder_free(builder);
    if (output.data || output.size) {
        fprintf(stderr, "JSON output buffer was not cleared\n");
        return 6;
    }
    puts("PASS: morph_json parse, query, build, serialize, and lifetime contract");
    return 0;
}
