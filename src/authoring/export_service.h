#ifndef MORPHEUS_EXPORT_SERVICE_H
#define MORPHEUS_EXPORT_SERVICE_H

#include <sys/types.h>

#include "morpheus/authoring.h"

typedef struct morph_export_service {
    char tool_path[MORPHEUS_AUTHORING_EXPORT_PATH_CAPACITY];
    char output_path[MORPHEUS_AUTHORING_EXPORT_PATH_CAPACITY];
    char log_path[MORPHEUS_AUTHORING_EXPORT_PATH_CAPACITY];
    pid_t process_id;
    morph_authoring_export_status status;
} morph_export_service;

void morph_export_service_init(
    morph_export_service *service,
    const char *tool_path);
void morph_export_service_reset(morph_export_service *service);
int morph_export_service_start(
    morph_export_service *service,
    const char *source_path,
    const char *output_path,
    const char *application_name,
    const char *bundle_identifier,
    const char *application_version,
    char *error,
    unsigned long error_capacity);
int morph_export_service_poll(
    morph_export_service *service,
    int *finished,
    char *error,
    unsigned long error_capacity);
void morph_export_service_cancel(morph_export_service *service);
int morph_export_service_read_log(
    const morph_export_service *service,
    char *output,
    unsigned long output_capacity);

#endif
