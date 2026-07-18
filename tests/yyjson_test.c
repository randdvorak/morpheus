#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yyjson.h"

int main(void)
{
    static const char input[] =
        "{\"name\":\"Morpheus\",\"revision\":7,\"capabilities\":[\"http\",\"storage\"]}";
    yyjson_doc *document = yyjson_read(input, sizeof(input) - 1, 0);
    yyjson_val *root;
    yyjson_val *name;
    yyjson_val *revision;
    yyjson_val *capabilities;
    yyjson_mut_doc *output_document;
    yyjson_mut_val *output_root;
    char *output;

    if (!document) {
        fprintf(stderr, "Unable to parse valid JSON\n");
        return 1;
    }
    root = yyjson_doc_get_root(document);
    name = yyjson_obj_get(root, "name");
    revision = yyjson_obj_get(root, "revision");
    capabilities = yyjson_obj_get(root, "capabilities");
    if (!yyjson_is_obj(root) || !yyjson_is_str(name) ||
        strcmp(yyjson_get_str(name), "Morpheus") != 0 ||
        !yyjson_is_int(revision) || yyjson_get_int(revision) != 7 ||
        !yyjson_is_arr(capabilities) || yyjson_arr_size(capabilities) != 2) {
        yyjson_doc_free(document);
        fprintf(stderr, "Parsed JSON values did not match\n");
        return 2;
    }
    yyjson_doc_free(document);

    if (yyjson_read("{\"invalid\":]", 13, 0) != NULL) {
        fprintf(stderr, "Invalid JSON was accepted\n");
        return 3;
    }

    output_document = yyjson_mut_doc_new(NULL);
    output_root = output_document ? yyjson_mut_obj(output_document) : NULL;
    if (!output_document || !output_root) {
        yyjson_mut_doc_free(output_document);
        fprintf(stderr, "Unable to allocate JSON document\n");
        return 4;
    }
    yyjson_mut_doc_set_root(output_document, output_root);
    if (!yyjson_mut_obj_add_str(output_document, output_root, "status", "ready") ||
        !yyjson_mut_obj_add_uint(output_document, output_root, "schema_version", 1)) {
        yyjson_mut_doc_free(output_document);
        fprintf(stderr, "Unable to construct JSON object\n");
        return 5;
    }
    output = yyjson_mut_write(output_document, 0, NULL);
    yyjson_mut_doc_free(output_document);
    if (!output || strcmp(output, "{\"status\":\"ready\",\"schema_version\":1}") != 0) {
        free(output);
        fprintf(stderr, "Serialized JSON did not match\n");
        return 6;
    }
    free(output);
    puts("PASS: embedded yyjson parsing, validation, and serialization");
    return 0;
}
