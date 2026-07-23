#include "config.h"
#include "registry.h"
#include "state.h"
#include "dispatch.h"
#include "output.h"
#include "cli.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void print_help(void) {
    printf("usage: dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]\n"
           "  subcommand: inject | clean | query | list\n"
           "  inject <uid> --p1=v1 ...     注入故障\n"
           "  clean  <uid> [--k1=v1 ...]   清除活跃注入（按参数匹配）\n"
           "  query  [uid] [--k=v ...]     无 uid 查询全部活跃；有 uid 验证故障生效\n"
           "  list                         列出故障目录\n");
}

int main(int argc, char **argv) {
    const char *cfgpath = NULL;
    int show_help = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) show_help = 1;
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) cfgpath = argv[++i];
    }
    if (show_help) { print_help(); return 0; }
    if (argc < 2) { print_help(); return 2; }

    char defcfg[512];
    if (!cfgpath) {
        ssize_t n = readlink("/proc/self/exe", defcfg, sizeof(defcfg) - 1);
        if (n > 0) {
            defcfg[n] = '\0';
            char *slash = strrchr(defcfg, '/');
            if (slash) *slash = '\0';
            char *bslash = strrchr(defcfg, '/');
            if (bslash) *bslash = '\0';
            snprintf(defcfg + strlen(defcfg), sizeof(defcfg) - strlen(defcfg),
                     "/config/demoncat.conf");
            cfgpath = defcfg;
        } else {
            cfgpath = "config/demoncat.conf";
        }
    }

    config_t cfg;
    if (config_load(cfgpath, &cfg) != 0) {
        fprintf(stderr, "config load failed: %s\n", cfgpath);
        return 1;
    }
    registry_init(&cfg);
    state_set_file(cfg.state_file[0] ? cfg.state_file : "~/.demoncat/state.json");
    state_load();

    parsed_cmd_t pc;
    if (cli_parse(argc, argv, &pc) != 0) {
        printf("{\"status\":\"error\",\"op\":\"parse\",\"error\":{\"code\":2,\"message\":\"parse error\"}}\n");
        return 2;
    }
    result_t *r = dispatch_route(pc.uid, pc.op, &pc.params);
    output_print(r);
    int code = r ? r->code : 1;
    result_free(r);
    return code;
}
