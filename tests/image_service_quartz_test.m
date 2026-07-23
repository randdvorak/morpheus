#include <stdio.h>

#define NK_IMPLEMENTATION
#include "morpheus/app_api.h"
#include "image_service.h"

static const unsigned char png_1x1_rgba[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
    0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
    0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99,
    0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
    0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

int main(void)
{
    static const unsigned char rgba_pixels[] = {
        255, 0, 0, 255, 0, 255, 0, 255
    };
    struct nk_context context;
    morph_image_service *images;
    morph_image_result result;
    morph_image_id decoded;
    morph_image_id rgba;

    if (!nk_init_default(&context, NULL)) return 1;
    images = morph_image_service_create(NULL, &context, NULL,
        MORPHEUS_IMAGE_BACKEND_QUARTZ);
    if (!images) return 2;
    decoded = morph_image_load_memory(images,
        png_1x1_rgba, sizeof(png_1x1_rgba));
    if (!decoded || !morph_image_poll(images, decoded, &result) ||
        result.status != MORPH_IMAGE_READY ||
        result.width != 1 || result.height != 1) return 3;
    rgba = morph_image_load_rgba(images, rgba_pixels, 2, 1);
    if (!rgba || !morph_image_poll(images, rgba, &result) ||
        result.status != MORPH_IMAGE_READY ||
        result.width != 2 || result.height != 1) return 4;
    morph_image_release(images, decoded);
    morph_image_release(images, rgba);
    if (morph_image_poll(images, rgba, &result)) return 5;
    morph_image_service_destroy(images);
    nk_free(&context);
    puts("PASS: Quartz image service decodes and retains CGImages");
    return 0;
}
