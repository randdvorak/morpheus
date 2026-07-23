#ifndef MORPHEUS_DATABASE_SERVICE_H
#define MORPHEUS_DATABASE_SERVICE_H

#include "morpheus/database.h"

#define MORPHEUS_DATABASE_MAX_STATEMENTS 64u
#define MORPHEUS_DATABASE_MAX_SQL (256u * 1024u)
#define MORPHEUS_DATABASE_MAX_VALUE (16u * 1024u * 1024u)
#define MORPHEUS_DATABASE_PATH_CAPACITY 4096u

typedef struct morph_database_service morph_database_service;

morph_database_service *morph_database_service_create(
    const char *path, char *error, unsigned long error_capacity);
void morph_database_service_destroy(morph_database_service *service);
morph_capability morph_database_service_capability(
    morph_database_service *service);
int morph_database_service_switch_path(
    morph_database_service *service, const char *path,
    char *error, unsigned long error_capacity);
int morph_database_service_begin_preview(
    morph_database_service *service, char *error, unsigned long error_capacity);
int morph_database_service_accept_preview(
    morph_database_service *service, char *error, unsigned long error_capacity);
int morph_database_service_reject_preview(
    morph_database_service *service, char *error, unsigned long error_capacity);
int morph_database_service_is_previewing(
    const morph_database_service *service);

#endif
