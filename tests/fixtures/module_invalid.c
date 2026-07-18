#include "morpheus/app_api.h"

static const morph_app_api app_api = {
    999u,
    "invalid-version",
    0,
    0,
    0,
    0,
    0,
    0
};

const morph_app_api *morph_app_entry(void)
{
    return &app_api;
}
