#include <stdio.h>
#include <string.h>

#include "http_service.h"

int main(void)
{
    morph_http_service *service;
    morph_http_request_id request;
    morph_http_request_id cancelled;
    morph_http_result result;
    char url[4096];
    const char *path = MORPHEUS_TEST_FIXTURE_ROOT "/http_response.json";
    size_t offset = 0;
    size_t index;
    unsigned int attempt;

    service = morph_http_service_create();
    if (!service) {
        fprintf(stderr, "HTTP service initialization failed\n");
        return 1;
    }
    memcpy(url, "file://", 7);
    offset = 7;
    for (index = 0; path[index] != '\0' && offset + 3 < sizeof(url); ++index) {
        if (path[index] == ' ') {
            memcpy(url + offset, "%20", 3);
            offset += 3;
        } else {
            url[offset++] = path[index];
        }
    }
    url[offset] = '\0';
    request = morph_http_get(service, url);
    cancelled = morph_http_get(service, url);
    if (!request || !cancelled) {
        fprintf(stderr, "HTTP request creation failed\n");
        morph_http_service_destroy(service);
        return 2;
    }
    morph_http_cancel(service, cancelled);

    memset(&result, 0, sizeof(result));
    for (attempt = 0; attempt < 100 && (!result.completed || result.failed); ++attempt) {
        if (!morph_http_poll(service, request, &result)) {
            fprintf(stderr, "HTTP request disappeared\n");
            morph_http_service_destroy(service);
            return 3;
        }
        if (result.completed) break;
    }
    if (!result.completed || result.failed || result.status_code != 0 ||
        strcmp(result.body, "{\"status\":\"morpheus-http-ready\"}\n") != 0) {
        fprintf(stderr, "HTTP response mismatch: completed=%d failed=%d error=%s body=%s\n",
            result.completed, result.failed, result.error ? result.error : "(none)",
            result.body ? result.body : "(null)");
        morph_http_service_destroy(service);
        return 4;
    }
    morph_http_cancel(service, request);
    morph_http_service_destroy(service);
    puts("PASS: asynchronous HTTP request, response capture, and cancellation");
    return 0;
}
