#ifndef MORPHEUS_IMAGE_SERVICE_H
#define MORPHEUS_IMAGE_SERVICE_H

#include "morpheus/sdk.h"

struct nk_context;

#define MORPHEUS_IMAGE_MAX_IMAGES 64
#define MORPHEUS_IMAGE_MAX_ENCODED (1024u * 1024u)
#define MORPHEUS_IMAGE_MAX_DIMENSION 4096u
#define MORPHEUS_IMAGE_MAX_PIXELS (16u * 1024u * 1024u)

morph_image_service *morph_image_service_create(
    void *metal_device,
    struct nk_context *nuklear,
    morph_http_service *http);
void morph_image_service_tick(morph_image_service *service);
void morph_image_service_destroy(morph_image_service *service);

#endif
