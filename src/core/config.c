#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
    return s;
}

int config_load(const char *path, config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    char root[256];
    derive_project_root(path, root, sizeof root);
    char line[512];
    fault_def_t *cur = NULL;
    char section[128] = "";
    while (fgets(line, sizeof(line), fp)) {
        char *p = trim(line);
        if (*p == '#' || *p == ';' || *p == '\0') continue;
        if (*p == '[') {
            char *e = strchr(p, ']');
            if (!e) continue;
            *e = '\0';
            strncpy(section, p + 1, sizeof(section) - 1);
            section[sizeof(section)-1] = '\0';
            cur = NULL;
            if (strncmp(section, "fault.", 6) == 0 && cfg->fault_count < DCAT_MAX_FAULTS) {
                cur = &cfg->faults[cfg->fault_count++];
                strncpy(cur->uid, section + 6, sizeof(cur->uid) - 1);
                cur->uid[sizeof(cur->uid)-1] = '\0';
            }
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = trim(p), *v = trim(eq + 1);
        if (strcmp(section, "demoncat") == 0) {
            if (strcmp(k, "state_file") == 0) { strncpy(cfg->state_file, v, sizeof(cfg->state_file)-1); cfg->state_file[sizeof(cfg->state_file)-1]='\0'; }
            else if (strcmp(k, "log_level") == 0) { strncpy(cfg->log_level, v, sizeof(cfg->log_level)-1); cfg->log_level[sizeof(cfg->log_level)-1]='\0'; }
        } else if (cur) {
            if      (strcmp(k, "module") == 0)          { strncpy(cur->module, v, sizeof(cur->module)-1); cur->module[sizeof(cur->module)-1]='\0'; }
            else if (strcmp(k, "desc") == 0)            { strncpy(cur->desc, v, sizeof(cur->desc)-1); cur->desc[sizeof(cur->desc)-1]='\0'; }
            else if (strcmp(k, "script") == 0)          { resolve_script(root, v, cur->script, sizeof(cur->script)); }
            else if (strcmp(k, "supported_ops") == 0)    { strncpy(cur->supported_ops, v, sizeof(cur->supported_ops)-1); cur->supported_ops[sizeof(cur->supported_ops)-1]='\0'; }
            else if (strcmp(k, "inject_required") == 0)  { strncpy(cur->inject_required, v, sizeof(cur->inject_required)-1); cur->inject_required[sizeof(cur->inject_required)-1]='\0'; }
            else if (strcmp(k, "inject_optional") == 0)  { strncpy(cur->inject_optional, v, sizeof(cur->inject_optional)-1); cur->inject_optional[sizeof(cur->inject_optional)-1]='\0'; }
            else if (strcmp(k, "clean_required") == 0)   { strncpy(cur->clean_required, v, sizeof(cur->clean_required)-1); cur->clean_required[sizeof(cur->clean_required)-1]='\0'; }
            else if (strcmp(k, "clean_optional") == 0)   { strncpy(cur->clean_optional, v, sizeof(cur->clean_optional)-1); cur->clean_optional[sizeof(cur->clean_optional)-1]='\0'; }
            else if (strcmp(k, "query_required") == 0)   { strncpy(cur->query_required, v, sizeof(cur->query_required)-1); cur->query_required[sizeof(cur->query_required)-1]='\0'; }
            else if (strcmp(k, "query_optional") == 0)   { strncpy(cur->query_optional, v, sizeof(cur->query_optional)-1); cur->query_optional[sizeof(cur->query_optional)-1]='\0'; }
        }
    }
    fclose(fp);
    return 0;
}

const fault_def_t *config_find(const config_t *cfg, const char *uid) {
    for (int i = 0; i < cfg->fault_count; i++)
        if (strcmp(cfg->faults[i].uid, uid) == 0) return &cfg->faults[i];
    return NULL;
}

void resolve_script(const char *root, const char *val, char *dst, int cap) {
    if (val[0] == '/' || val[0] == '~' || strcmp(root, ".") == 0) {
        strncpy(dst, val, cap - 1); dst[cap - 1] = '\0';
    } else {
        snprintf(dst, cap, "%s/%s", root, val);
    }
}

void derive_project_root(const char *cfgpath, char *root, int cap) {
    /* <root>/config/demoncat.conf → <root>；相对路径 → '.' */
    const char *marker = "/config/demoncat.conf";
    size_t mlen = strlen(marker);
    size_t plen = strlen(cfgpath);
    if (plen >= mlen && strcmp(cfgpath + plen - mlen, marker) == 0) {
        size_t rlen = plen - mlen;
        if (rlen == 0) { strncpy(root, "/", cap-1); root[cap-1]='\0'; return; }
        if (rlen >= (size_t)cap) rlen = cap - 1;
        memcpy(root, cfgpath, rlen); root[rlen] = '\0';
    } else {
        strncpy(root, ".", cap - 1); root[cap - 1] = '\0';
    }
}
