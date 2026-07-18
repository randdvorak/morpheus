#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "http_service.h"

typedef struct test_server {
    int socket;
    unsigned short port;
    pthread_t thread;
} test_server;

static void *serve_one_request(void *opaque)
{
    static const char body[] = "{\"status\":\"morpheus-http-ready\"}\n";
    char response[512];
    char request[1024];
    test_server *server = (test_server *)opaque;
    int client = accept(server->socket, NULL, NULL);
    if (client >= 0) {
        int length;
        (void)recv(client, request, sizeof(request), 0);
        length = snprintf(
            response,
            sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n\r\n%s",
            (unsigned long)(sizeof(body) - 1),
            body);
        if (length > 0) (void)send(client, response, (size_t)length, 0);
        close(client);
    }
    return NULL;
}

static int start_server(test_server *server)
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
    address.sin_port = 0;
    if (bind(server->socket, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(server->socket, (struct sockaddr *)&address, &address_size) != 0 ||
        listen(server->socket, 2) != 0) {
        close(server->socket);
        return 0;
    }
    server->port = ntohs(address.sin_port);
    if (pthread_create(&server->thread, NULL, serve_one_request, server) != 0) {
        close(server->socket);
        return 0;
    }
    return 1;
}

static void stop_server(test_server *server)
{
    (void)pthread_join(server->thread, NULL);
    close(server->socket);
}

int main(void)
{
    static const struct timespec poll_delay = {0, 10000000};
    morph_http_service *service;
    morph_http_request_id request;
    morph_http_request_id cancelled;
    morph_http_result result;
    test_server server;
    char url[128];
    unsigned int attempt;

    if (!start_server(&server)) {
        fprintf(stderr, "Unable to start loopback HTTP server\n");
        return 1;
    }
    service = morph_http_service_create();
    if (!service) {
        fprintf(stderr, "HTTP service initialization failed\n");
        stop_server(&server);
        return 2;
    }
    snprintf(url, sizeof(url), "http://127.0.0.1:%u/fixture", server.port);
    request = morph_http_get(service, url);
    if (!request) {
        fprintf(stderr, "HTTP request creation failed\n");
        morph_http_service_destroy(service);
        stop_server(&server);
        return 3;
    }

    memset(&result, 0, sizeof(result));
    for (attempt = 0; attempt < 500 && !result.completed; ++attempt) {
        if (!morph_http_poll(service, request, &result)) {
            fprintf(stderr, "HTTP request disappeared\n");
            morph_http_service_destroy(service);
            stop_server(&server);
            return 4;
        }
        if (!result.completed) (void)nanosleep(&poll_delay, NULL);
    }
    stop_server(&server);
    if (!result.completed || result.failed || result.status_code != 200 ||
        strcmp(result.body, "{\"status\":\"morpheus-http-ready\"}\n") != 0) {
        fprintf(stderr, "HTTP response mismatch: completed=%d failed=%d error=%s body=%s\n",
            result.completed, result.failed, result.error ? result.error : "(none)",
            result.body ? result.body : "(null)");
        morph_http_service_destroy(service);
        return 5;
    }
    morph_http_cancel(service, request);

    cancelled = morph_http_get(service, "http://127.0.0.1:1/cancelled");
    if (!cancelled) {
        fprintf(stderr, "Cancellation request creation failed\n");
        morph_http_service_destroy(service);
        return 6;
    }
    morph_http_cancel(service, cancelled);
    if (morph_http_poll(service, cancelled, &result)) {
        fprintf(stderr, "Cancelled request remained visible\n");
        morph_http_service_destroy(service);
        return 7;
    }

    morph_http_service_destroy(service);
    puts("PASS: asynchronous HTTP request, response capture, and cancellation");
    return 0;
}
