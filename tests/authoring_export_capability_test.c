#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "authoring_capabilities.h"
#include "export_service.h"

int main(void)
{
    static const char tool_script[] =
        "#!/bin/sh\n"
        "test -n \"${MORPHEUS_CMAKE_COMMAND:-}\"\n"
        "test -x \"$MORPHEUS_CMAKE_COMMAND\"\n"
        "printf '%256s\\n' diagnostic-prefix\n"
        "printf 'export-tool-tail-marker\\n'\n";
    char source[] = "/private/tmp/morpheus-export-source-XXXXXX";
    char tool[] = "/private/tmp/morpheus-export-tool-XXXXXX";
    const char *output = "morpheus-export-result.app";
    char log[128];
    char error[512] = {0};
    morph_export_service service;
    morph_capability entry;
    morph_capability_registry registry;
    morph_host authoring_host = {0};
    const morph_capability *provider;
    const morph_authoring_export_api *export_api;
    void *context;
    struct timespec delay = {0, 1000000};
    int descriptor;
    int finished = 0;
    int polls = 0;

    descriptor = mkstemp(source);
    if (descriptor < 0) {
        perror("mkstemp");
        return 1;
    }
    close(descriptor);
    descriptor = mkstemp(tool);
    if (descriptor < 0 ||
        write(descriptor, tool_script, sizeof(tool_script) - 1) !=
            (ssize_t)(sizeof(tool_script) - 1) ||
        fchmod(descriptor, 0700) != 0) {
        perror("create export tool");
        if (descriptor >= 0) close(descriptor);
        unlink(source);
        unlink(tool);
        return 1;
    }
    close(descriptor);

    entry = morph_authoring_export_capability(&service, tool);
    registry.entries = &entry;
    registry.count = 1;
    authoring_host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    authoring_host.capabilities = &registry;
    provider = morph_host_find_capability(
        &authoring_host,
        MORPHEUS_AUTHORING_EXPORT_CAPABILITY,
        MORPHEUS_AUTHORING_EXPORT_ABI_VERSION);
    export_api = morph_authoring_export_from_capability(provider);
    context = provider ? provider->context : NULL;
    if (!export_api ||
        !export_api->start(
            context,
            source,
            output,
            "Capability Export",
            "dev.morpheus.test-export",
            "1.2.3",
            error,
            sizeof(error))) {
        fprintf(stderr, "unable to start export capability: %s\n", error);
        unlink(source);
        unlink(tool);
        return 2;
    }

    while (!finished && polls++ < 500) {
        if (!export_api->poll(context, &finished, error, sizeof(error))) {
            fprintf(stderr, "export poll failed: %s\n", error);
            export_api->reset(context);
            unlink(source);
            unlink(tool);
            return 3;
        }
        if (!finished) nanosleep(&delay, NULL);
    }
    if (!finished ||
        export_api->status(context) != MORPHEUS_AUTHORING_EXPORT_SUCCEEDED ||
        !export_api->output_path(context) ||
        !strstr(export_api->output_path(context),
            "/exports/morpheus-export-result.app") ||
        !export_api->read_log(context, log, sizeof(log)) ||
        !strstr(log, "export-tool-tail-marker")) {
        fprintf(stderr, "export capability did not report success\n");
        export_api->reset(context);
        unlink(source);
        unlink(tool);
        return 4;
    }

    export_api->reset(context);
    unlink(source);
    unlink(tool);
    puts("PASS: asynchronous export lifecycle through authoring capability");
    return 0;
}
