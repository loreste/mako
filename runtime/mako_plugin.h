/* Mako plugin ABI v1 — native dynamic plugins and WASM plugin manifests.
 *
 * This header is intentionally small and C-compatible. Host/plugin ownership
 * stays explicit: strings returned by a plugin must be released through the
 * plugin's optional `free_string` callback when present.
 */
#ifndef MAKO_PLUGIN_H
#define MAKO_PLUGIN_H

#include "mako_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_PLUGIN_ABI_VERSION 1u
#define MAKO_PLUGIN_API_VERSION "mako.plugin.v1"

#if defined(_WIN32) || defined(_WIN64)
#define MAKO_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MAKO_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    uint32_t abi_version;
    const char *api_version;
    const char *name;
    const char *version;
    const char *kind; /* "native" or "wasm" */
} MakoPluginInfo;

typedef struct {
    uint32_t abi_version;
    void (*log)(int32_t level, MakoString message);
    void *user_data;
} MakoPluginHost;

typedef struct {
    uint32_t abi_version;
    MakoPluginInfo info;
    int32_t (*init)(const MakoPluginHost *host);
    void (*shutdown)(void);
    MakoString (*call)(MakoString operation, MakoString payload);
    void (*free_string)(MakoString value);
} MakoPluginVTable;

typedef const MakoPluginVTable *(*MakoPluginEntryFn)(void);

MAKO_PLUGIN_EXPORT const MakoPluginVTable *mako_plugin_entry(void);

static inline int mako_plugin_abi_compatible(uint32_t abi_version) {
    return abi_version == MAKO_PLUGIN_ABI_VERSION;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_PLUGIN_H */
