#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static void copystr(char *dst, size_t cap, const char *src) {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* Derive project root from the config file path:
 * config is always at <root>/config/demoncat.conf, so root = dirname(dirname(path)).
 * Used to resolve relative script paths (e.g. "config/scripts/foo.sh") to absolute
 * so dcat works regardless of the caller's CWD. */
static void derive_project_root(const char *cfgpath, char *root, size_t cap) {
    if (!cfgpath || !root || cap == 0) { if (root && cap) root[0] = '\0'; return; }
    copystr(root, cap, cfgpath);
    /* dirname once: strip "/demoncat.conf" -> "<root>/config" */
    char *slash = strrchr(root, '/');
    if (slash) *slash = '\0'; else { root[0] = '.'; root[1] = '\0'; return; }
    /* dirname twice: strip "/config" -> "<root>" */
    slash = strrchr(root, '/');
    if (slash && slash != root) *slash = '\0';
    else if (slash) { root[0] = '/'; root[1] = '\0'; }  /* root filesystem */
    else { root[0] = '.'; root[1] = '\0'; }
}

/* Resolve a script path: if already absolute or home-relative, keep as-is;
 * otherwise prepend project root so dcat runs from any CWD. When root is "."
 * or empty (config loaded by relative path), keep the script path as-is so
 * it resolves relative to CWD (the historical behavior tests rely on). */
static void resolve_script(const char *root, const char *val, char *dst, size_t cap) {
    if (!val || !dst || cap == 0) return;
    if (val[0] == '/' || val[0] == '~') { copystr(dst, cap, val); return; }
    if (!root[0] || !strcmp(root, ".")) { copystr(dst, cap, val); return; }
    snprintf(dst, cap, "%s/%s", root, val);
}

static safety_level_t parse_safety(const char *v) {
    if (!strcmp(v, "warning")) return SAFETY_WARNING;
    if (!strcmp(v, "dangerous")) return SAFETY_DANGEROUS;
    return SAFETY_NORMAL;
}

static exec_mode_t parse_mode(const char *v) {
    if (!strcmp(v, "background")) return EXEC_BACKGROUND;
    return EXEC_SYNC;
}

int config_load(const char *path, config_t *cfg) {
    if (!path || !cfg) return -1;
    memset(cfg, 0, sizeof *cfg);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char root[256];
    derive_project_root(path, root, sizeof root);

    char line[512];
    char section[80] = "";
    fault_def_t *cur = NULL;

    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == ';' || *p == '#') continue;
        if (*p == '[') {
            char *e = strchr(p, ']');
            if (!e) continue;
            *e = '\0';
            char *sec = trim(p + 1);
            copystr(section, sizeof section, sec);
            if (!strncmp(sec, "fault.", 6)) {
                if (cfg->fault_count >= DCAT_MAX_FAULTS) { cur = NULL; continue; }
                cur = &cfg->faults[cfg->fault_count++];
                memset(cur, 0, sizeof *cur);
                copystr(cur->uid, sizeof cur->uid, sec + 6);
            } else {
                cur = NULL;
            }
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);
        if (!strcmp(section, "demoncat")) {
            if (!strcmp(key, "state_file")) copystr(cfg->state_file, sizeof cfg->state_file, val);
            else if (!strcmp(key, "log_level")) copystr(cfg->log_level, sizeof cfg->log_level, val);
        } else if (cur) {
            if (!strcmp(key, "module")) copystr(cur->module, sizeof cur->module, val);
            else if (!strcmp(key, "desc")) copystr(cur->desc, sizeof cur->desc, val);
            else if (!strcmp(key, "script")) resolve_script(root, val, cur->script, sizeof cur->script);
            else if (!strcmp(key, "supported_ops")) copystr(cur->supported_ops, sizeof cur->supported_ops, val);
            else if (!strcmp(key, "required_params")) copystr(cur->required_params, sizeof cur->required_params, val);
            else if (!strcmp(key, "optional_params")) copystr(cur->optional_params, sizeof cur->optional_params, val);
            else if (!strcmp(key, "safety")) cur->safety = parse_safety(val);
            else if (!strcmp(key, "exec_mode")) cur->exec_mode = parse_mode(val);
            else if (!strcmp(key, "timeout")) cur->timeout = atoi(val);
        }
    }
    fclose(f);
    return 0;
}
