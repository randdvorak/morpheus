#include <stdlib.h>

#include "morpheus/app_api.h"
#include "morpheus/runtime.h"

extern const morph_app_api *morph_app_entry(void);

__attribute__((weak)) unsigned int morph_app_render_mode(void)
{
    return MORPHEUS_RENDER_EMBEDDED;
}

int main(void)
{
    const morph_runtime_config config = {
        .fallback_name = MORPHEUS_EXPORT_APP_NAME,
        .fallback_bundle_identifier = MORPHEUS_EXPORT_BUNDLE_ID,
        .render_mode = morph_app_render_mode(),
        .window_width = 1000,
        .window_height = 700,
        .capabilities = NULL
    };
    return morph_runtime_run(morph_app_entry(), &config);
}
