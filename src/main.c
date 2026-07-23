/* src/main.c */
#include "core/cli.h"
#include "core/config.h"
#include "core/registry.h"
#include "core/state.h"
#include "core/output.h"
#include "core/dispatch.h"
#include "core/executor.h"

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void copystr_safe(char *dst, size_t cap, const char *src) {
    if (!dst || !src || cap == 0) return;
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static const char *default_config_path(void) {
    static char path[512];
    char exe[256];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return NULL;
    exe[n] = '\0';
    char exe_copy[256];
    copystr_safe(exe_copy, sizeof(exe_copy), exe);
    char *dir = dirname(exe_copy);
    snprintf(path, sizeof(path), "%s/../config/demoncat.conf", dir);
    return path;
}

static void expand_state_path(const char *in, char *out, size_t cap) {
    if (in[0] == '~') {
        const char *home = getenv("HOME");
        snprintf(out, cap, "%s%s", home ? home : "", in + 1);
    } else {
        copystr_safe(out, cap, in);
    }
}

int main(int argc, char **argv) {
    parsed_cmd_t pc;
    if (cli_parse(argc, argv, &pc) != 0) {
        output_print(result_err("", "", DCAT_E_PARSE, "parse error"));
        return DCAT_E_PARSE;
    }

    if (pc.help) {
        printf("usage: dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]\n");
        printf("  subcommands: inject, clean, query, list\n");
        return 0;
    }

    const char *cfgpath = pc.config_path[0] ? pc.config_path : default_config_path();
    config_t cfg;
    if (!cfgpath || config_load(cfgpath, &cfg)) {
        output_print(result_err(pc.op, pc.uid, DCAT_E_RUN, "config not found"));
        return DCAT_E_RUN;
    }
    registry_init(&cfg);

    char sp[256];
    expand_state_path(cfg.state_file[0] ? cfg.state_file : "~/.demoncat/state.json",
                      sp, sizeof(sp));
    state_init(sp);
    state_load();

    result_t *res = NULL;
    if (!strcmp(pc.op, "list")) {
        res = dispatch_list();
    } else if (!strcmp(pc.op, "query")) {
        res = dispatch_query(pc.uid, &pc.params);
    } else if (!strcmp(pc.op, "inject")) {
        res = dispatch_inject(pc.uid, &pc.params);
    } else if (!strcmp(pc.op, "clean")) {
        res = dispatch_clean(pc.uid, &pc.params);
    } else {
        res = result_err(pc.op, "", DCAT_E_PARSE, "unknown op");
    }

    output_print(res);
    int code = res ? res->code : 0;
    result_free(res);
    return code;
}
