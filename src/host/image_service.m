#include "morpheus/app_api.h"
#include "image_service.h"

#include <Metal/Metal.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

typedef struct morph_image_job {
    morph_image_id id;
    morph_http_request_id request_id;
    void *retained_texture;
    unsigned int width;
    unsigned int height;
    morph_image_status status;
    char error[192];
} morph_image_job;

struct morph_image_service {
    void *retained_device;
    struct nk_context *nuklear;
    morph_http_service *http;
    morph_image_job jobs[MORPHEUS_IMAGE_MAX_IMAGES];
    morph_image_id next_id;
};

static morph_image_job *morph_image_find(
    morph_image_service *service,
    morph_image_id image_id)
{
    unsigned int index;
    if (!service || !image_id) return NULL;
    for (index = 0; index < MORPHEUS_IMAGE_MAX_IMAGES; ++index) {
        if (service->jobs[index].id == image_id) return &service->jobs[index];
    }
    return NULL;
}

static morph_image_job *morph_image_allocate(morph_image_service *service)
{
    unsigned int index;
    if (!service) return NULL;
    for (index = 0; index < MORPHEUS_IMAGE_MAX_IMAGES; ++index) {
        morph_image_job *job = &service->jobs[index];
        if (!job->id) {
            memset(job, 0, sizeof(*job));
            job->id = ++service->next_id;
            if (!job->id) job->id = ++service->next_id;
            return job;
        }
    }
    return NULL;
}

static void morph_image_fail(morph_image_job *job, const char *message)
{
    job->status = MORPH_IMAGE_FAILED;
    snprintf(job->error, sizeof(job->error), "%s", message ? message : "Image load failed");
}

static int morph_image_decode(
    morph_image_service *service,
    morph_image_job *job,
    const void *data,
    unsigned long size)
{
    id<MTLDevice> device;
    id<MTLTexture> texture;
    MTLTextureDescriptor *descriptor;
    unsigned char *pixels;
    int width;
    int height;
    int channels;

    if (!data || !size || size > MORPHEUS_IMAGE_MAX_ENCODED || size > INT_MAX) {
        morph_image_fail(job, "Encoded image is empty or exceeds the 1 MiB limit");
        return 0;
    }
    if (!stbi_info_from_memory(data, (int)size, &width, &height, &channels) ||
        width <= 0 || height <= 0 ||
        (unsigned int)width > MORPHEUS_IMAGE_MAX_DIMENSION ||
        (unsigned int)height > MORPHEUS_IMAGE_MAX_DIMENSION ||
        (unsigned long)width * (unsigned long)height > MORPHEUS_IMAGE_MAX_PIXELS) {
        morph_image_fail(job, "Image is invalid or exceeds dimension limits");
        return 0;
    }
    pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        morph_image_fail(job, stbi_failure_reason());
        return 0;
    }

    device = (__bridge id<MTLDevice>)service->retained_device;
    descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:(NSUInteger)width
        height:(NSUInteger)height
        mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    texture = [device newTextureWithDescriptor:descriptor];
    if (texture) {
        [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
            mipmapLevel:0
            withBytes:pixels
            bytesPerRow:(NSUInteger)width * 4u];
    }
    stbi_image_free(pixels);
    if (!texture) {
        morph_image_fail(job, "Unable to create the GPU texture");
        return 0;
    }
    job->retained_texture = (__bridge_retained void *)texture;
    job->width = (unsigned int)width;
    job->height = (unsigned int)height;
    job->status = MORPH_IMAGE_READY;
    return 1;
}

morph_image_service *morph_image_service_create(
    void *metal_device,
    struct nk_context *nuklear,
    morph_http_service *http)
{
    morph_image_service *service;
    id<MTLDevice> device = (__bridge id<MTLDevice>)metal_device;
    if (!device) return NULL;
    service = calloc(1, sizeof(*service));
    if (!service) return NULL;
    service->retained_device = (__bridge_retained void *)device;
    service->nuklear = nuklear;
    service->http = http;
    return service;
}

morph_image_id morph_image_load_memory(
    morph_image_service *service,
    const void *data,
    unsigned long size)
{
    morph_image_job *job = morph_image_allocate(service);
    if (!job) return 0;
    (void)morph_image_decode(service, job, data, size);
    return job->id;
}

morph_image_id morph_image_load_url(morph_image_service *service, const char *url)
{
    morph_image_job *job;
    if (!service || !service->http) return 0;
    job = morph_image_allocate(service);
    if (!job) return 0;
    job->request_id = morph_http_get(service->http, url);
    if (!job->request_id) morph_image_fail(job, "Unable to start image URL request");
    return job->id;
}

void morph_image_service_tick(morph_image_service *service)
{
    unsigned int index;
    if (!service || !service->http) return;
    for (index = 0; index < MORPHEUS_IMAGE_MAX_IMAGES; ++index) {
        morph_image_job *job = &service->jobs[index];
        morph_http_result result;
        if (!job->id || !job->request_id || job->status != MORPH_IMAGE_PENDING) continue;
        if (!morph_http_poll(service->http, job->request_id, &result) || !result.completed) continue;
        if (result.failed || result.status_code < 200 || result.status_code >= 300) {
            morph_image_fail(job, result.error && *result.error
                ? result.error : "Image URL returned an unsuccessful status");
        } else {
            (void)morph_image_decode(service, job, result.body, result.body_size);
        }
        morph_http_cancel(service->http, job->request_id);
        job->request_id = 0;
    }
}

int morph_image_poll(
    morph_image_service *service,
    morph_image_id image_id,
    morph_image_result *result)
{
    morph_image_job *job = morph_image_find(service, image_id);
    if (!job || !result) return 0;
    result->status = job->status;
    result->width = job->width;
    result->height = job->height;
    result->error = job->error;
    return 1;
}

int morph_image_draw(morph_image_service *service, morph_image_id image_id)
{
    morph_image_job *job = morph_image_find(service, image_id);
    if (!job || job->status != MORPH_IMAGE_READY || !service->nuklear) return 0;
    nk_image(service->nuklear, nk_image_ptr(job->retained_texture));
    return 1;
}

void morph_image_release(morph_image_service *service, morph_image_id image_id)
{
    morph_image_job *job = morph_image_find(service, image_id);
    if (!job) return;
    if (job->request_id && service->http) morph_http_cancel(service->http, job->request_id);
    if (job->retained_texture) CFRelease(job->retained_texture);
    memset(job, 0, sizeof(*job));
}

void morph_image_service_destroy(morph_image_service *service)
{
    unsigned int index;
    if (!service) return;
    for (index = 0; index < MORPHEUS_IMAGE_MAX_IMAGES; ++index) {
        if (service->jobs[index].id) morph_image_release(service, service->jobs[index].id);
    }
    if (service->retained_device) CFRelease(service->retained_device);
    free(service);
}
