#include <Metal/Metal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NK_IMPLEMENTATION
#include "morpheus/app_api.h"
#include "http_service.h"
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

typedef struct image_server {
    int socket;
    unsigned short port;
    pthread_t thread;
} image_server;

static void *serve_image(void *opaque)
{
    image_server *server = opaque;
    char request[512];
    char header[256];
    int client = accept(server->socket, NULL, NULL);
    if (client >= 0) {
        int length;
        (void)recv(client, request, sizeof(request), 0);
        length = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n"
            "Content-Length: %lu\r\nConnection: close\r\n\r\n",
            (unsigned long)sizeof(png_1x1_rgba));
        if (length > 0) (void)send(client, header, (size_t)length, 0);
        (void)send(client, png_1x1_rgba, sizeof(png_1x1_rgba), 0);
        close(client);
    }
    return NULL;
}

static int start_server(image_server *server)
{
    struct sockaddr_in address;
    socklen_t address_size = sizeof(address);
    int reuse = 1;
    memset(server, 0, sizeof(*server));
    server->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket < 0) return 0;
    (void)setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server->socket, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(server->socket, (struct sockaddr *)&address, &address_size) != 0 ||
        listen(server->socket, 1) != 0) return 0;
    server->port = ntohs(address.sin_port);
    return pthread_create(&server->thread, NULL, serve_image, server) == 0;
}

int main(void)
{
    static const struct timespec delay = {0, 10000000};
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    morph_http_service *http;
    morph_image_service *images;
    morph_image_result result;
    morph_image_id memory_image;
    morph_image_id rgba_image;
    morph_image_id url_image;
    morph_image_id invalid_image;
    morph_image_id invalid_rgba_image;
    static const unsigned char rgba_pixels[] = {
        255, 0, 0, 255, 0, 255, 0, 255
    };
    image_server server;
    char url[128];
    unsigned int attempt;

    if (!device) return 77;
    http = morph_http_service_create();
    images = morph_image_service_create((__bridge void *)device, NULL, http);
    if (!http || !images) return 1;

    memory_image = morph_image_load_memory(images, png_1x1_rgba, sizeof(png_1x1_rgba));
    if (!memory_image || !morph_image_poll(images, memory_image, &result) ||
        result.status != MORPH_IMAGE_READY || result.width != 1 || result.height != 1) return 2;

    rgba_image = morph_image_load_rgba(images, rgba_pixels, 2, 1);
    if (!rgba_image || !morph_image_poll(images, rgba_image, &result) ||
        result.status != MORPH_IMAGE_READY || result.width != 2 || result.height != 1) return 8;

    invalid_rgba_image = morph_image_load_rgba(images, NULL, 1, 1);
    if (!invalid_rgba_image || !morph_image_poll(images, invalid_rgba_image, &result) ||
        result.status != MORPH_IMAGE_FAILED || !result.error || !*result.error) return 9;

    invalid_image = morph_image_load_memory(images, "bad", 3);
    if (!invalid_image || !morph_image_poll(images, invalid_image, &result) ||
        result.status != MORPH_IMAGE_FAILED || !result.error || !*result.error) return 3;

    if (!start_server(&server)) return 4;
    snprintf(url, sizeof(url), "http://127.0.0.1:%u/image.png", server.port);
    url_image = morph_image_load_url(images, url);
    memset(&result, 0, sizeof(result));
    for (attempt = 0; attempt < 500 && result.status == MORPH_IMAGE_PENDING; ++attempt) {
        morph_image_service_tick(images);
        if (!morph_image_poll(images, url_image, &result)) return 5;
        if (result.status == MORPH_IMAGE_PENDING) nanosleep(&delay, NULL);
    }
    pthread_join(server.thread, NULL);
    close(server.socket);
    if (result.status != MORPH_IMAGE_READY || result.width != 1 || result.height != 1) return 6;

    morph_image_release(images, memory_image);
    morph_image_release(images, rgba_image);
    morph_image_release(images, invalid_rgba_image);
    morph_image_release(images, invalid_image);
    morph_image_release(images, url_image);
    if (morph_image_poll(images, url_image, &result)) return 7;
    morph_image_service_destroy(images);
    morph_http_service_destroy(http);
    puts("PASS: bounded memory and asynchronous URL image loading");
    return 0;
}
