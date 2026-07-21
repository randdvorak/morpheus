#ifndef MORPHEUS_AGENT_SESSION_H
#define MORPHEUS_AGENT_SESSION_H

#include <sys/types.h>

#define MORPH_AGENT_PATH_CAPACITY 4096
#define MORPH_AGENT_REQUEST_CAPACITY 2048
#define MORPH_AGENT_MODEL_CAPACITY 256
#define MORPH_AGENT_MAX_ATTEMPTS 3
#define MORPH_AGENT_CANDIDATE_MAX_BYTES (1024UL * 1024UL)
#define MORPH_AGENT_FAILURE_CAPACITY 512
#define MORPH_AGENT_DIGEST_SIZE 32

typedef enum morph_agent_status {
    MORPH_AGENT_IDLE = 0,
    MORPH_AGENT_RUNNING,
    MORPH_AGENT_PROVIDER_SUCCEEDED,
    MORPH_AGENT_PROVIDER_FAILED
} morph_agent_status;

typedef struct morph_agent_session {
    char root[MORPH_AGENT_PATH_CAPACITY];
    char provider_path[MORPH_AGENT_PATH_CAPACITY];
    char run_directory[MORPH_AGENT_PATH_CAPACITY];
    char candidate_path[MORPH_AGENT_PATH_CAPACITY];
    char source_before_path[MORPH_AGENT_PATH_CAPACITY];
    char request_path[MORPH_AGENT_PATH_CAPACITY];
    char prompt_path[MORPH_AGENT_PATH_CAPACITY];
    char diagnostics_path[MORPH_AGENT_PATH_CAPACITY];
    char response_path[MORPH_AGENT_PATH_CAPACITY];
    char provider_log_path[MORPH_AGENT_PATH_CAPACITY];
    char provider_model[MORPH_AGENT_MODEL_CAPACITY];
    char failure_reason[MORPH_AGENT_FAILURE_CAPACITY];
    unsigned char provider_baseline[MORPH_AGENT_DIGEST_SIZE];
    unsigned long run_id;
    unsigned int attempt;
    pid_t process_id;
    morph_agent_status status;
    int provider_baseline_valid;
} morph_agent_session;

void morph_agent_session_reset(morph_agent_session *session);
int morph_agent_session_init(
    morph_agent_session *session,
    const char *root,
    const char *provider_path,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_begin(
    morph_agent_session *session,
    const char *request,
    const char *source_path,
    const char *api_header_path,
    const char *sdk_header_path,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_set_model(
    morph_agent_session *session,
    const char *model,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_start_attempt(
    morph_agent_session *session,
    const char *diagnostics,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_poll(
    morph_agent_session *session,
    int *finished,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_record_build(
    const morph_agent_session *session,
    int succeeded,
    const char *stage,
    const char *diagnostics,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_create_patch(
    const morph_agent_session *session,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_candidate_changed(
    const morph_agent_session *session,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_accept_source(
    const morph_agent_session *session,
    const char *destination_path,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_restore_source(
    const morph_agent_session *session,
    const char *destination_path,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_record_outcome(
    const morph_agent_session *session,
    const char *outcome,
    unsigned long revision,
    char *error,
    unsigned long error_capacity);
int morph_agent_session_read_provider_log(
    const morph_agent_session *session,
    char *output,
    unsigned long output_capacity);
void morph_agent_session_cancel(morph_agent_session *session);

#endif
