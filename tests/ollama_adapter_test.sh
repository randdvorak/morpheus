#!/bin/sh
set -eu

adapter=$1
fake_curl=$2
workspace=$(mktemp -d /private/tmp/morpheus-ollama-adapter-XXXXXX)
trap '/bin/rm -rf "$workspace"' EXIT

printf '%s\n' 'int original_candidate;' > "$workspace/candidate.c"
printf '%s\n' \
    'typedef struct morph_host { void (*ui_label)(struct morph_host *, const char *); } morph_host;' \
    > "$workspace/app_api.h"
printf '%s\n' \
    'void *morph_json_parse(const char *, unsigned long, void *);' \
    'unsigned long morph_image_load_url(void *, const char *);' \
    > "$workspace/sdk.h"
: > "$workspace/model.txt"
printf '%s\n' 'Replace the candidate for this test.' > "$workspace/prompt.txt"
: > "$workspace/diagnostics.txt"

MORPHEUS_CURL_EXECUTABLE=$fake_curl \
MORPHEUS_OLLAMA_URL=http://localhost:11434 \
    "$adapter" \
    "$workspace" \
    "$workspace/prompt.txt" \
    "$workspace/diagnostics.txt" \
    "$workspace/response.txt"

/usr/bin/grep -q 'repaired_by_ollama' "$workspace/candidate.c"
/usr/bin/grep -q 'local model updated candidate' "$workspace/response.txt"
/usr/bin/jq -e '.model == "fake-coder:latest"' "$workspace/ollama-request.json" >/dev/null
FAKE_OLLAMA_FENCED=1 \
MORPHEUS_CURL_EXECUTABLE=$fake_curl \
MORPHEUS_OLLAMA_URL=http://localhost:11434 \
    "$adapter" \
    "$workspace" \
    "$workspace/prompt.txt" \
    "$workspace/diagnostics.txt" \
    "$workspace/fenced-response.txt"
/usr/bin/grep -q 'morph_app_entry' "$workspace/candidate.c"
/usr/bin/grep -q 'fenced source' "$workspace/fenced-response.txt"
printf '%s\n' 'PASS: Ollama model discovery, structured request, and candidate extraction'
