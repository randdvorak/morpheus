#ifndef MORPHEUS_APP_API_H
#define MORPHEUS_APP_API_H

#define MORPHEUS_HOST_ABI_VERSION 1u
#define MORPHEUS_APP_ABI_VERSION 1u

typedef struct morph_host morph_host;

struct morph_host {
    unsigned int abi_version;
    void *user_data;
    void (*log)(morph_host *host, const char *message);
    void (*ui_label)(morph_host *host, const char *text);
    int (*ui_button)(morph_host *host, const char *text);
};

typedef struct morph_app_api {
    unsigned int abi_version;
    const char *name;
    int (*create)(morph_host *host, void **state);
    void (*destroy)(morph_host *host, void *state);
    void (*update)(morph_host *host, void *state, double dt);
    void (*render_ui)(morph_host *host, void *state);
} morph_app_api;

typedef const morph_app_api *(*morph_app_entry_fn)(void);

#endif
