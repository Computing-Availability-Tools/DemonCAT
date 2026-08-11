#include "plugin_manager.h"
#include "precheck.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <dirent.h>

static const dcat_plugin_t *g_plugins[DCAT_MAX_PLUGINS];
static void *g_handles[DCAT_MAX_PLUGINS];
static int g_count = 0;

int plugin_load_dir(const char *dir) {
    if (!dir) return 0;
    int loaded = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (g_count >= DCAT_MAX_PLUGINS) break;
        const char *name = e->d_name;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 3, ".so") != 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            fprintf(stderr, "plugin: dlopen %s failed: %s\n", path, dlerror());
            continue;
        }
        const dcat_plugin_t *(*get)(void) = (const dcat_plugin_t *(*)(void))dlsym(h, "dcat_plugin_get");
        if (!get) {
            fprintf(stderr, "plugin: %s missing dcat_plugin_get: %s\n", path, dlerror());
            dlclose(h);
            continue;
        }
        const dcat_plugin_t *p = get();
        if (!p) {
            fprintf(stderr, "plugin: %s dcat_plugin_get returned NULL\n", path);
            dlclose(h);
            continue;
        }
        if (p->abi_version != DCAT_PLUGIN_ABI_VERSION) {
            fprintf(stderr, "plugin: %s abi_version %d != %d, rejected\n",
                    path, p->abi_version, DCAT_PLUGIN_ABI_VERSION);
            dlclose(h);
            continue;
        }
        if (!p->uid || !p->uid[0] || !p->name || !p->supported_ops || !p->supported_ops[0]) {
            fprintf(stderr, "plugin: %s missing required fields (uid/name/supported_ops)\n", path);
            dlclose(h);
            continue;
        }
        if (op_in_supported(p->supported_ops, "inject") && !p->inject) {
            fprintf(stderr, "plugin: %s supported_ops declares inject but inject() is NULL\n", path);
            dlclose(h);
            continue;
        }
        if (op_in_supported(p->supported_ops, "clean") && !p->clean) {
            fprintf(stderr, "plugin: %s supported_ops declares clean but clean() is NULL\n", path);
            dlclose(h);
            continue;
        }
        if (op_in_supported(p->supported_ops, "query") && !p->query) {
            fprintf(stderr, "plugin: %s supported_ops declares query but query() is NULL\n", path);
            dlclose(h);
            continue;
        }
        {
            int dup = 0;
            for (int i = 0; i < g_count; i++)
                if (strcmp(g_plugins[i]->uid, p->uid) == 0) {
                    dup = 1;
                    break;
                }
            if (dup) {
                fprintf(stderr, "plugin: %s duplicate uid '%s' already loaded\n", path, p->uid);
                dlclose(h);
                continue;
            }
        }
        if (p->init && p->init() != 0) {
            fprintf(stderr, "plugin: %s init failed\n", path);
            dlclose(h);
            continue;
        }
        g_plugins[g_count] = p;
        g_handles[g_count] = h;
        g_count++;
        loaded++;
    }
    closedir(d);
    return loaded;
}

const dcat_plugin_t *plugin_find(const char *uid) {
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_plugins[i]->uid, uid) == 0) return g_plugins[i];
    return NULL;
}

int plugin_count(void) { return g_count; }

const dcat_plugin_t *const *plugin_list(int *count) {
    if (count) *count = g_count;
    return g_plugins;
}

void plugin_fini(void) {
    for (int i = 0; i < g_count; i++) {
        if (g_plugins[i] && g_plugins[i]->fini) g_plugins[i]->fini();
        if (g_handles[i]) dlclose(g_handles[i]);
        g_handles[i] = NULL;
        g_plugins[i] = NULL;
    }
    g_count = 0;
}
