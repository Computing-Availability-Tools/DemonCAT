#include "cli.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *valid_subcommands[] = {"inject", "clean", "query", "list", NULL};
static char g_cli_error[256] = "";

const char *cli_get_error(void) { return g_cli_error; }

static int is_subcommand(const char *s) {
    if (!s) return 0;
    for (int i = 0; valid_subcommands[i]; i++)
        if (strcmp(s, valid_subcommands[i]) == 0) return 1;
    return 0;
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
    g_cli_error[0] = '\0';
    if (argc < 2) {
        snprintf(g_cli_error, sizeof g_cli_error, "no subcommand given (available: inject, clean, query, list)");
        return -1;
    }

    out->op = is_subcommand(argv[1]) ? argv[1] : NULL;
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
        /* subcommand may appear after global options (e.g. dcat --config x.conf inject ...) */
        if (!out->op && is_subcommand(argv[i])) {
            out->op = argv[i];
            /* uid may follow */
            if (i + 1 < argc && argv[i+1][0] != '-' && argv[i+1][0] != '\0' &&
                strcmp(argv[i+1], "values") != 0 && strcmp(argv[i+1], "where") != 0) {
                strncpy(out->uid, argv[i+1], sizeof(out->uid) - 1);
                out->uid[sizeof(out->uid) - 1] = '\0';
                i++;
            }
            continue;
        }
        if (strncmp(argv[i], "--", 2) != 0) {
            snprintf(g_cli_error, sizeof g_cli_error, "unexpected argument '%s' (expected --key=value)", argv[i]);
            return -1;
        }
        const char *kv = argv[i] + 2;
        const char *eq = strchr(kv, '=');
        if (!eq) {
            snprintf(g_cli_error, sizeof g_cli_error, "invalid parameter '%s' (expected --key=value)", argv[i]);
            return -1;
        }
        char key[64];
        size_t kl = (size_t)(eq - kv);
        if (kl >= sizeof(key)) {
            snprintf(g_cli_error, sizeof g_cli_error, "parameter name too long in '%s'", argv[i]);
            return -1;
        }
        memcpy(key, kv, kl); key[kl] = '\0';
        const char *val = eq + 1;
        if (params_set(&out->params, key, val) != 0) {
            snprintf(g_cli_error, sizeof g_cli_error, "too many parameters (max %d)", DCAT_MAX_PARAMS);
            return -1;
        }
    }
    if (!out->op && !out->help) {
        if (argc >= 2 && !is_subcommand(argv[1])) {
            snprintf(g_cli_error, sizeof g_cli_error, "unknown subcommand '%s' (available: inject, clean, query, list)", argv[1]);
        }
        return -1;
    }
    return 0;
}
