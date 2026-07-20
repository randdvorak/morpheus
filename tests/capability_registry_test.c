#include <stddef.h>

#include "morpheus/app_api.h"

int main(void)
{
    static const int compiler_v1 = 1;
    static const int compiler_v2 = 2;
    static const int export_v1 = 3;
    const morph_capability entries[] = {
        {"dev.morpheus.authoring.compiler", 1u, sizeof(compiler_v1), &compiler_v1, NULL},
        {"dev.morpheus.authoring.export", 1u, sizeof(export_v1), &export_v1, NULL},
        {"dev.morpheus.authoring.compiler", 2u, sizeof(compiler_v2), &compiler_v2, NULL}
    };
    const morph_capability_registry registry = {
        entries,
        sizeof(entries) / sizeof(entries[0])
    };
    const morph_host host = {
        .abi_version = MORPHEUS_HOST_ABI_VERSION,
        .capabilities = &registry
    };
    const morph_capability_registry invalid_registry = {NULL, 1u};
    const morph_host invalid_host = {
        .abi_version = MORPHEUS_HOST_ABI_VERSION,
        .capabilities = &invalid_registry
    };
    const morph_capability *capability;

    capability = morph_host_find_capability(
        &host, "dev.morpheus.authoring.compiler", 1u);
    if (!capability || capability->abi_version != 2u ||
        capability->api != &compiler_v2) return 1;
    capability = morph_host_find_capability(
        &host, "dev.morpheus.authoring.compiler", 3u);
    if (capability) return 2;
    capability = morph_host_find_capability(
        &host, "dev.morpheus.authoring.missing", 1u);
    if (capability) return 3;
    if (morph_host_find_capability(NULL, "dev.morpheus.authoring.compiler", 1u)) {
        return 4;
    }
    if (morph_host_find_capability(
            &invalid_host, "dev.morpheus.authoring.compiler", 1u)) return 5;
    return 0;
}
