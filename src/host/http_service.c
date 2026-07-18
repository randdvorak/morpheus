#include "http_service.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct morph_http_job {
    CURL *easy;
    struct curl_slist *headers;
    morph_http_request_id id;
    char *request_body;
    char *response_body;
    unsigned long response_size;
    unsigned long response_capacity;
    long status_code;
    int active;
    int completed;
    int failed;
    char error[256];
} morph_http_job;

struct morph_http_service {
    CURLM *multi;
    morph_http_job jobs[MORPHEUS_HTTP_MAX_REQUESTS];
    morph_http_request_id next_id;
};

static morph_http_job *morph_http_find(
    morph_http_service *service,
    morph_http_request_id request_id)
{
    unsigned int index;
    for (index = 0; index < MORPHEUS_HTTP_MAX_REQUESTS; ++index) {
        if (service->jobs[index].id == request_id) return &service->jobs[index];
    }
    return NULL;
}

static size_t morph_http_write(
    char *data,
    size_t size,
    size_t count,
    void *opaque)
{
    morph_http_job *job = (morph_http_job *)opaque;
    unsigned long incoming = (unsigned long)(size * count);
    unsigned long required;
    char *expanded;

    if (incoming > MORPHEUS_HTTP_MAX_RESPONSE - job->response_size) return 0;
    required = job->response_size + incoming + 1;
    if (required > job->response_capacity) {
        unsigned long capacity = job->response_capacity ? job->response_capacity * 2 : 4096;
        while (capacity < required && capacity < MORPHEUS_HTTP_MAX_RESPONSE) {
            capacity *= 2;
        }
        if (capacity > MORPHEUS_HTTP_MAX_RESPONSE) capacity = MORPHEUS_HTTP_MAX_RESPONSE;
        expanded = (char *)realloc(job->response_body, capacity);
        if (!expanded) return 0;
        job->response_body = expanded;
        job->response_capacity = capacity;
    }
    memcpy(job->response_body + job->response_size, data, incoming);
    job->response_size += incoming;
    job->response_body[job->response_size] = '\0';
    return incoming;
}

static morph_http_request_id morph_http_start(
    morph_http_service *service,
    const char *url,
    const char *method,
    const char *body,
    unsigned long body_size)
{
    morph_http_job *job = NULL;
    unsigned int index;
    CURLMcode result;
    morph_http_request_id request_id;

    if (!service || !service->multi || !url || !*url) return 0;
    for (index = 0; index < MORPHEUS_HTTP_MAX_REQUESTS; ++index) {
        if (!service->jobs[index].active && !service->jobs[index].completed) {
            job = &service->jobs[index];
            break;
        }
    }
    if (!job) return 0;

    request_id = ++service->next_id;
    if (request_id == 0) request_id = ++service->next_id;
    memset(job, 0, sizeof(*job));
    job->id = request_id;
    job->easy = curl_easy_init();
    if (!job->easy) {
        job->id = 0;
        return 0;
    }
    if (body_size != 0) {
        job->request_body = (char *)malloc(body_size + 1);
        if (!job->request_body) {
            curl_easy_cleanup(job->easy);
            memset(job, 0, sizeof(*job));
            return 0;
        }
        memcpy(job->request_body, body, body_size);
        job->request_body[body_size] = '\0';
    }

    curl_easy_setopt(job->easy, CURLOPT_URL, url);
    curl_easy_setopt(job->easy, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(job->easy, CURLOPT_WRITEFUNCTION, morph_http_write);
    curl_easy_setopt(job->easy, CURLOPT_WRITEDATA, job);
    curl_easy_setopt(job->easy, CURLOPT_PRIVATE, job);
    curl_easy_setopt(job->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(job->easy, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(job->easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(job->easy, CURLOPT_TIMEOUT_MS, 30000L);
    if (body) {
        curl_easy_setopt(job->easy, CURLOPT_POSTFIELDS, job->request_body);
        curl_easy_setopt(job->easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_size);
        job->headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(job->easy, CURLOPT_HTTPHEADER, job->headers);
    }
    result = curl_multi_add_handle(service->multi, job->easy);
    if (result != CURLM_OK) {
        snprintf(job->error, sizeof(job->error), "Unable to start HTTP request: %s",
            curl_multi_strerror(result));
        curl_easy_cleanup(job->easy);
        free(job->request_body);
        curl_slist_free_all(job->headers);
        memset(job, 0, sizeof(*job));
        return 0;
    }
    job->active = 1;
    return request_id;
}

static int morph_http_service_init(morph_http_service *service)
{
    if (!service) return 0;
    memset(service, 0, sizeof(*service));
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 0;
    service->multi = curl_multi_init();
    if (!service->multi) {
        curl_global_cleanup();
        return 0;
    }
    return 1;
}

morph_http_service *morph_http_service_create(void)
{
    morph_http_service *service = (morph_http_service *)calloc(1, sizeof(*service));
    if (!service || !morph_http_service_init(service)) {
        free(service);
        return NULL;
    }
    return service;
}

void morph_http_service_tick(morph_http_service *service)
{
    int running = 0;
    if (!service || !service->multi) return;
    (void)curl_multi_perform(service->multi, &running);
}

morph_http_request_id morph_http_get(morph_http_service *service, const char *url)
{
    return morph_http_start(service, url, "GET", NULL, 0);
}

morph_http_request_id morph_http_post_json(
    morph_http_service *service,
    const char *url,
    const char *json,
    unsigned long json_size)
{
    if (!json) return 0;
    return morph_http_start(service, url, "POST", json, json_size);
}

int morph_http_poll(
    morph_http_service *service,
    morph_http_request_id request_id,
    morph_http_result *result)
{
    morph_http_job *job;
    CURLMsg *message;
    int messages = 0;

    if (!service || !result) return 0;
    morph_http_service_tick(service);
    job = morph_http_find(service, request_id);
    if (!job) return 0;
    while ((message = curl_multi_info_read(service->multi, &messages)) != NULL) {
        morph_http_job *completed = NULL;
        curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &completed);
        if (!completed) continue;
        completed->active = 0;
        completed->completed = 1;
        completed->failed = message->data.result != CURLE_OK;
        if (completed->failed) {
            snprintf(completed->error, sizeof(completed->error), "%s",
                curl_easy_strerror(message->data.result));
        }
        (void)curl_easy_getinfo(completed->easy, CURLINFO_RESPONSE_CODE,
            &completed->status_code);
        curl_multi_remove_handle(service->multi, completed->easy);
    }
    memset(result, 0, sizeof(*result));
    result->completed = job->completed;
    result->failed = job->failed;
    result->status_code = job->status_code;
    result->body = job->response_body ? job->response_body : "";
    result->body_size = job->response_size;
    result->error = job->error;
    return 1;
}

void morph_http_cancel(morph_http_service *service, morph_http_request_id request_id)
{
    morph_http_job *job;
    if (!service) return;
    job = morph_http_find(service, request_id);
    if (!job) return;
    if (job->active) curl_multi_remove_handle(service->multi, job->easy);
    curl_easy_cleanup(job->easy);
    free(job->request_body);
    free(job->response_body);
    curl_slist_free_all(job->headers);
    memset(job, 0, sizeof(*job));
}

void morph_http_service_destroy(morph_http_service *service)
{
    unsigned int index;
    if (!service) return;
    for (index = 0; index < MORPHEUS_HTTP_MAX_REQUESTS; ++index) {
        if (service->jobs[index].id != 0) {
            morph_http_cancel(service, service->jobs[index].id);
        }
    }
    if (service->multi) curl_multi_cleanup(service->multi);
    service->multi = NULL;
    curl_global_cleanup();
    free(service);
}
