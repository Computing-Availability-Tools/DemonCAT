/* src/core/cli.c */
#include "cli.h"

#include <string.h>

static int is_subcommand(const char *s) {
    return !strcmp(s, "inject") || !strcmp(s, "clean") ||
           !strcmp(s, "query")  || !strcmp(s, "list");
}

int cli_parse(int argc, char **argv, parsed_cmd_t *out) {
    if (!argv || !out || argc < 2) return -1;
    memset(out, 0, sizeof *out);

    int i = 1;

    /* scan for --help anywhere; also find subcommand (first non-flag arg) */
    for (int j = 1; j < argc; j++) {
        if (!strcmp(argv[j], "--help") || !strcmp(argv[j], "-h")) {
            out->help = 1;
            return 0;
        }
    }

    /* skip --config <path> at front */
    while (i < argc && !strcmp(argv[i], "--config")) {
        if (i + 1 >= argc) return -1; /* --config needs a value */
        strncpy(out->config_path, argv[i + 1], sizeof(out->config_path) - 1);
        out->config_path[sizeof(out->config_path) - 1] = '\0';
        i += 2;
    }

    /* subcommand */
    if (i >= argc) return -1;
    if (!is_subcommand(argv[i])) return -1;
    out->op = argv[i];
    i++;

    /* uid (if present and not a --flag) */
    if (i < argc && argv[i][0] != '-') {
        strncpy(out->uid, argv[i], sizeof(out->uid) - 1);
        out->uid[sizeof(out->uid) - 1] = '\0';
        i++;
    } else if (out->op && (!strcmp(out->op, "inject") || !strcmp(out->op, "clean"))) {
        /* inject and clean require uid */
        return -1;
    }

    /* remaining: --key=value flags and --config <path> */
    while (i < argc) {
        if (!strcmp(argv[i], "--config")) {
            if (i + 1 >= argc) return -1;
            strncpy(out->config_path, argv[i + 1], sizeof(out->config_path) - 1);
            out->config_path[sizeof(out->config_path) - 1] = '\0';
            i += 2;
            continue;
        }
        /* must start with -- and contain = */
        if (argv[i][0] != '-' || argv[i][1] != '-') return -1;
        char *eq = strchr(argv[i], '=');
        if (!eq) return -1;
        /* extract key (skip --) and value */
        const char *kstart = argv[i] + 2;
        size_t klen = eq - kstart;
        if (klen == 0 || klen >= DCAT_KEY_LEN) return -1;
        if (out->params.count >= DCAT_MAX_PARAMS) return -1;
        strncpy(out->params.items[out->params.count].key, kstart, klen);
        out->params.items[out->params.count].key[klen] = '\0';
        const char *vstart = eq + 1;
        strncpy(out->params.items[out->params.count].value, vstart, DCAT_VAL_LEN - 1);
        out->params.items[out->params.count].value[DCAT_VAL_LEN - 1] = '\0';
        out->params.count++;
        i++;
    }

    return 0;
}
