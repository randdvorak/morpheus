#include "http_service.h"

#include <Foundation/Foundation.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct morph_http_job {
    morph_http_request_id id;
    NSUInteger task_identifier;
    void *retained_task;
    char *response_body;
    unsigned long response_size;
    unsigned long response_capacity;
    long status_code;
    unsigned int redirect_count;
    int active;
    int completed;
    int failed;
    char error[256];
} morph_http_job;

struct morph_http_service {
    pthread_mutex_t mutex;
    pthread_cond_t invalidated_condition;
    morph_http_job jobs[MORPHEUS_HTTP_MAX_REQUESTS];
    morph_http_request_id next_id;
    void *retained_session;
    void *retained_delegate;
    int invalidated;
    int shutting_down;
};

@interface MorphHTTPDelegate : NSObject <NSURLSessionDataDelegate, NSURLSessionTaskDelegate>
@property(nonatomic, assign) morph_http_service *service;
@end

static NSURLSession *morph_http_session(morph_http_service *service)
{
    return service && service->retained_session
        ? (__bridge NSURLSession *)service->retained_session
        : nil;
}

static morph_http_job *morph_http_find_locked(
    morph_http_service *service,
    morph_http_request_id request_id)
{
    unsigned int index;
    for (index = 0; index < MORPHEUS_HTTP_MAX_REQUESTS; ++index) {
        if (service->jobs[index].id == request_id) return &service->jobs[index];
    }
    return NULL;
}

static morph_http_job *morph_http_find_task_locked(
    morph_http_service *service,
    NSUInteger task_identifier)
{
    unsigned int index;
    for (index = 0; index < MORPHEUS_HTTP_MAX_REQUESTS; ++index) {
        morph_http_job *job = &service->jobs[index];
        if (job->id && job->task_identifier == task_identifier) return job;
    }
    return NULL;
}

static void morph_http_set_error(morph_http_job *job, NSString *message)
{
    const char *utf8 = message.length ? message.UTF8String : "HTTP request failed";
    snprintf(job->error, sizeof(job->error), "%s", utf8 ? utf8 : "HTTP request failed");
}

static int morph_http_append(morph_http_job *job, const void *bytes, unsigned long size)
{
    unsigned long required;
    unsigned long capacity;
    char *expanded;

    if (size > MORPHEUS_HTTP_MAX_RESPONSE - job->response_size) return 0;
    required = job->response_size + size + 1;
    if (required > job->response_capacity) {
        capacity = job->response_capacity ? job->response_capacity * 2 : 4096;
        while (capacity < required && capacity < MORPHEUS_HTTP_MAX_RESPONSE + 1u) {
            capacity *= 2;
        }
        if (capacity > MORPHEUS_HTTP_MAX_RESPONSE + 1u) {
            capacity = MORPHEUS_HTTP_MAX_RESPONSE + 1u;
        }
        expanded = (char *)realloc(job->response_body, (size_t)capacity);
        if (!expanded) return 0;
        job->response_body = expanded;
        job->response_capacity = capacity;
    }
    if (size) memcpy(job->response_body + job->response_size, bytes, (size_t)size);
    job->response_size += size;
    job->response_body[job->response_size] = '\0';
    return 1;
}

@implementation MorphHTTPDelegate

- (void)URLSession:(NSURLSession *)session
        dataTask:(NSURLSessionDataTask *)dataTask
        didReceiveResponse:(NSURLResponse *)response
        completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler
{
    morph_http_service *service = self.service;
    BOOL reject = NO;
    (void)session;

    if (!service) {
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    pthread_mutex_lock(&service->mutex);
    morph_http_job *job = morph_http_find_task_locked(service, dataTask.taskIdentifier);
    if (!job || service->shutting_down) {
        reject = YES;
    } else {
        if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
            job->status_code = ((NSHTTPURLResponse *)response).statusCode;
        }
        if (response.expectedContentLength > (long long)MORPHEUS_HTTP_MAX_RESPONSE) {
            job->failed = 1;
            morph_http_set_error(job, @"HTTP response exceeded the 1 MiB limit");
            reject = YES;
        }
    }
    pthread_mutex_unlock(&service->mutex);
    completionHandler(reject ? NSURLSessionResponseCancel : NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session
        dataTask:(NSURLSessionDataTask *)dataTask
        didReceiveData:(NSData *)data
{
    morph_http_service *service = self.service;
    BOOL cancel = NO;
    (void)session;
    if (!service) return;

    pthread_mutex_lock(&service->mutex);
    morph_http_job *job = morph_http_find_task_locked(service, dataTask.taskIdentifier);
    if (job && !service->shutting_down &&
        !morph_http_append(job, data.bytes, (unsigned long)data.length)) {
        job->failed = 1;
        morph_http_set_error(job, @"HTTP response exceeded the 1 MiB limit");
        cancel = YES;
    }
    pthread_mutex_unlock(&service->mutex);
    if (cancel) [dataTask cancel];
}

- (void)URLSession:(NSURLSession *)session
        task:(NSURLSessionTask *)task
        willPerformHTTPRedirection:(NSHTTPURLResponse *)response
        newRequest:(NSURLRequest *)request
        completionHandler:(void (^)(NSURLRequest * _Nullable))completionHandler
{
    morph_http_service *service = self.service;
    NSURLRequest *redirect = request;
    (void)session;
    (void)response;

    if (!service) {
        completionHandler(nil);
        return;
    }
    pthread_mutex_lock(&service->mutex);
    morph_http_job *job = morph_http_find_task_locked(service, task.taskIdentifier);
    NSString *scheme = request.URL.scheme.lowercaseString;
    if (!job || service->shutting_down || ++job->redirect_count > 5 ||
        !([scheme isEqualToString:@"http"] || [scheme isEqualToString:@"https"])) {
        if (job) {
            job->failed = 1;
            morph_http_set_error(job, @"HTTP redirect limit or scheme rejected");
        }
        redirect = nil;
    }
    pthread_mutex_unlock(&service->mutex);
    completionHandler(redirect);
}

- (void)URLSession:(NSURLSession *)session
        task:(NSURLSessionTask *)task
        didCompleteWithError:(NSError *)error
{
    morph_http_service *service = self.service;
    (void)session;
    if (!service) return;

    pthread_mutex_lock(&service->mutex);
    morph_http_job *job = morph_http_find_task_locked(service, task.taskIdentifier);
    if (job) {
        job->active = 0;
        job->completed = 1;
        if (error && !job->failed) {
            job->failed = 1;
            morph_http_set_error(job, error.localizedDescription);
        }
        if (!job->response_body) (void)morph_http_append(job, "", 0);
    }
    pthread_mutex_unlock(&service->mutex);
}

- (void)URLSession:(NSURLSession *)session didBecomeInvalidWithError:(NSError *)error
{
    morph_http_service *service = self.service;
    (void)session;
    (void)error;
    if (!service) return;
    pthread_mutex_lock(&service->mutex);
    service->invalidated = 1;
    pthread_cond_signal(&service->invalidated_condition);
    pthread_mutex_unlock(&service->mutex);
}

@end

static morph_http_request_id morph_http_start(
    morph_http_service *service,
    const char *url_text,
    const char *method,
    const char *body,
    unsigned long body_size)
{
    morph_http_job *job = NULL;
    morph_http_request_id request_id;
    NSURLSessionDataTask *task;
    unsigned int index;

    if (!service || !url_text || !*url_text) return 0;
    NSString *text = [NSString stringWithUTF8String:url_text];
    NSURL *url = text ? [NSURL URLWithString:text] : nil;
    NSString *scheme = url.scheme.lowercaseString;
    if (!url || !([scheme isEqualToString:@"http"] || [scheme isEqualToString:@"https"])) {
        return 0;
    }

    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    request.HTTPMethod = [NSString stringWithUTF8String:method];
    if (body) {
        request.HTTPBody = [NSData dataWithBytes:body length:(NSUInteger)body_size];
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    }

    pthread_mutex_lock(&service->mutex);
    if (service->shutting_down) {
        pthread_mutex_unlock(&service->mutex);
        return 0;
    }
    for (index = 0; index < MORPHEUS_HTTP_MAX_REQUESTS; ++index) {
        if (!service->jobs[index].id) {
            job = &service->jobs[index];
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&service->mutex);
        return 0;
    }
    request_id = ++service->next_id;
    if (!request_id) request_id = ++service->next_id;
    memset(job, 0, sizeof(*job));
    job->id = request_id;
    task = [morph_http_session(service) dataTaskWithRequest:request];
    if (!task) {
        memset(job, 0, sizeof(*job));
        pthread_mutex_unlock(&service->mutex);
        return 0;
    }
    job->task_identifier = task.taskIdentifier;
    job->retained_task = (__bridge_retained void *)task;
    job->active = 1;
    pthread_mutex_unlock(&service->mutex);
    [task resume];
    return request_id;
}

morph_http_service *morph_http_service_create(void)
{
    morph_http_service *service = (morph_http_service *)calloc(1, sizeof(*service));
    if (!service) return NULL;
    if (pthread_mutex_init(&service->mutex, NULL) != 0) {
        free(service);
        return NULL;
    }
    if (pthread_cond_init(&service->invalidated_condition, NULL) != 0) {
        pthread_mutex_destroy(&service->mutex);
        free(service);
        return NULL;
    }

    MorphHTTPDelegate *delegate = [[MorphHTTPDelegate alloc] init];
    delegate.service = service;
    NSOperationQueue *queue = [[NSOperationQueue alloc] init];
    queue.maxConcurrentOperationCount = 1;
    queue.name = @"dev.morpheus.http";
    NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    configuration.timeoutIntervalForRequest = 5.0;
    configuration.timeoutIntervalForResource = 30.0;
    configuration.URLCache = nil;
    configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration
        delegate:delegate
        delegateQueue:queue];
    if (!session) {
        pthread_cond_destroy(&service->invalidated_condition);
        pthread_mutex_destroy(&service->mutex);
        free(service);
        return NULL;
    }
    service->retained_delegate = (__bridge_retained void *)delegate;
    service->retained_session = (__bridge_retained void *)session;
    return service;
}

void morph_http_service_tick(morph_http_service *service)
{
    (void)service;
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
    if (!service || !request_id || !result) return 0;
    pthread_mutex_lock(&service->mutex);
    job = morph_http_find_locked(service, request_id);
    if (!job) {
        pthread_mutex_unlock(&service->mutex);
        return 0;
    }
    memset(result, 0, sizeof(*result));
    result->completed = job->completed;
    result->failed = job->failed;
    result->status_code = job->status_code;
    result->body = job->completed && job->response_body ? job->response_body : "";
    result->body_size = job->completed ? job->response_size : 0;
    result->error = job->error;
    pthread_mutex_unlock(&service->mutex);
    return 1;
}

void morph_http_cancel(morph_http_service *service, morph_http_request_id request_id)
{
    NSURLSessionDataTask *task = nil;
    char *response_body = NULL;
    void *retained_task = NULL;
    if (!service || !request_id) return;

    pthread_mutex_lock(&service->mutex);
    morph_http_job *job = morph_http_find_locked(service, request_id);
    if (job) {
        retained_task = job->retained_task;
        if (retained_task) task = (__bridge NSURLSessionDataTask *)retained_task;
        response_body = job->response_body;
        memset(job, 0, sizeof(*job));
    }
    pthread_mutex_unlock(&service->mutex);
    if (task) [task cancel];
    if (retained_task) CFRelease(retained_task);
    free(response_body);
}

void morph_http_service_destroy(morph_http_service *service)
{
    unsigned int index;
    NSURLSession *session;
    MorphHTTPDelegate *delegate;
    if (!service) return;

    pthread_mutex_lock(&service->mutex);
    service->shutting_down = 1;
    pthread_mutex_unlock(&service->mutex);
    for (index = 0; index < MORPHEUS_HTTP_MAX_REQUESTS; ++index) {
        morph_http_request_id id;
        pthread_mutex_lock(&service->mutex);
        id = service->jobs[index].id;
        pthread_mutex_unlock(&service->mutex);
        if (id) morph_http_cancel(service, id);
    }

    session = morph_http_session(service);
    delegate = service->retained_delegate
        ? (__bridge MorphHTTPDelegate *)service->retained_delegate
        : nil;
    [session invalidateAndCancel];
    pthread_mutex_lock(&service->mutex);
    while (!service->invalidated) {
        pthread_cond_wait(&service->invalidated_condition, &service->mutex);
    }
    pthread_mutex_unlock(&service->mutex);
    delegate.service = NULL;

    if (service->retained_session) CFRelease(service->retained_session);
    if (service->retained_delegate) CFRelease(service->retained_delegate);
    pthread_cond_destroy(&service->invalidated_condition);
    pthread_mutex_destroy(&service->mutex);
    free(service);
}
