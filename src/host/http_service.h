#ifndef MORPHEUS_HTTP_SERVICE_H
#define MORPHEUS_HTTP_SERVICE_H

#include "morpheus/sdk.h"

#define MORPHEUS_HTTP_MAX_REQUESTS 8
#define MORPHEUS_HTTP_MAX_RESPONSE (1024u * 1024u)

morph_http_service *morph_http_service_create(void);
void morph_http_service_tick(morph_http_service *service);
void morph_http_service_destroy(morph_http_service *service);

#endif
