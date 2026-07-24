#include "cli.h"
#include <string.h>
#include <stdlib.h>

static int is_subcommand(const char *s) {
    if (!s) return 0;
    return strcmp(s, "inject") == 0 || strcmp(s, "clean") == 0 ||
           strcmp(s, "query") == 0 || strcmp(s, "list") == 0;
}

const char *cli_subcommand(int argc, char **argv) {
    if (argc < 2 || !is_subcommand(argv[1])) return NULL;
    return argv[1];
}

int cli_has_help(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        if (argv[i] && strcmp(argv[i], "--help") == 0) return 1;
    return 0;
}

int cli_parse(int argc, char **argv, parsed_cmd_t *out) {
    memset(out, 0, sizeof(*out));
    params_init(&out->params);
    if (argc < 2) return -1;

    out->op = is_subcommand(argv[1]) ? argv[1] : NULL;

    /* argv[1] 非子命令（如 --help/--config）时，从 argv[1] 起扫描全局选项 */
    int i = out->op ? 2 : 1;

    /* uid：紧跟子命令、非 flag、非保留字 */
    if (out->op && i < argc && argv[i][0] != '-' && argv[i][0] != '\0' &&
        strcmp(argv[i], "values") != 0 && strcmp(argv[i], "where") != 0) {
        strncpy(out->uid, argv[i], sizeof(out->uid) - 1);
        out->uid[sizeof(out->uid) - 1] = '\0';
        i++;
    }

    for (; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            out->help = 1;
            continue;
        }
        if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "--plugins") == 0) {
            if (i + 1 < argc) {
                if (strcmp(argv[i], "--config") == 0) out->config  = argv[i + 1];
                else                                    out->plugins = argv[i + 1];
                i++;
            }
            continue;
        }
        if (strncmp(argv[i], "--", 2) != 0) return -1;
        const char *kv = argv[i] + 2;
        const char *eq = strchr(kv, '=');
        if (!eq) return -1;
        char key[64];
        size_t kl = (size_t)(eq - kv);
        if (kl >= sizeof(key)) return -1;
        memcpy(key, kv, kl); key[kl] = '\0';
        const char *val = eq + 1;
        if (params_set(&out->params, key, val) != 0) return -1;
    }
    return 0;
}
