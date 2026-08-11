#include "cli.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *valid_subcommands[] = {"inject", "clean", "query", "list", "serve", NULL};
static char g_cli_error[256] = "";

const char *cli_get_error(void) { return g_cli_error; }

static int is_subcommand(const char *s) {
    if (!s) return 0;
    for (int i = 0; valid_subcommands[i]; i++)
        if (strcmp(s, valid_subcommands[i]) == 0) return 1;
    return 0;
}

static int has_param_after(int argc, char **argv, int from) {
    for (int j = from; j < argc; j++) {
        if (argv[j][0] == '-') return 1;
        if (strchr(argv[j], '=')) return 1;
    }
    return 0;
}

/* 严格解析端口:1-65535 整数,否则 -1 并写�?g_cli_error */
static int parse_port_arg(const char *s, int *out) {
    if (!s || !*s) {
        snprintf(g_cli_error, sizeof g_cli_error, "--port requires a numeric value 1-65535, got: '%s'", s ? s : "");
        return -1;
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1 || v > 65535) {
        snprintf(g_cli_error, sizeof g_cli_error, "--port requires a numeric value 1-65535, got: '%s'", s);
        return -1;
    }
    *out = (int)v;
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
        snprintf(g_cli_error, sizeof g_cli_error, "no subcommand; available: inject, clean, query, list, serve");
        return -1;
    }

    out->op = is_subcommand(argv[1]) ? argv[1] : NULL;
    int i = out->op ? 2 : 1;

    /* uid：紧跟子命令、非 flag、非保留�?*/
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
        if (strcmp(argv[i], "--force") == 0) {
            out->force = 1;
            continue;
        }
        if (strncmp(argv[i], "--force=", 8) == 0) {
            snprintf(g_cli_error, sizeof g_cli_error,
                     "--force does not take a value; use bare '--force'");
            return -1;
        }
        if (strcmp(argv[i], "--all") == 0) {
            out->all = 1;
            continue;
        }
        if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "--plugins") == 0) {
            if (i + 1 < argc) {
                if (strcmp(argv[i], "--config") == 0) out->config  = argv[i + 1];
                else                                    out->plugins = argv[i + 1];
                i++;
            } else {
                snprintf(g_cli_error, sizeof g_cli_error, "option '%s' requires a value (use '%s=<path>')", argv[i], argv[i]);
                return -1;
            }
            continue;
        }
        if (strncmp(argv[i], "--config=", 9) == 0) {
            if (argv[i][9] == '\0') {
                snprintf(g_cli_error, sizeof g_cli_error, "option '--config' requires a value (use '--config=<path>')");
                return -1;
            }
            out->config = argv[i] + 9;
            continue;
        }
        if (strncmp(argv[i], "--plugins=", 10) == 0) {
            if (argv[i][10] == '\0') {
                snprintf(g_cli_error, sizeof g_cli_error, "option '--plugins' requires a value (use '--plugins=<dir>')");
                return -1;
            }
            out->plugins = argv[i] + 10;
            continue;
        }
        /* serve 专用选项:�?serve 子命令解�?--port/--bind/--webroot/--allow-write)�?         * 避免 --port 全局吞掉 rNET_port_occupy/rNET_tcp_loss �?port 参数(撞名�?exit 3)�?*/
        if (out->op && strcmp(out->op, "serve") == 0) {
            if (strcmp(argv[i], "--allow-write") == 0) {
                out->allow_write = 1;
                continue;
            }
            if (strcmp(argv[i], "--port") == 0) {
                if (i + 1 < argc) {
                    if (parse_port_arg(argv[i + 1], &out->port) != 0) return -1;
                    i++;
                } else {
                    snprintf(g_cli_error, sizeof g_cli_error, "option '--port' requires a value (use '--port=<n>')");
                    return -1;
                }
                continue;
            }
            if (strncmp(argv[i], "--port=", 7) == 0) {
                if (parse_port_arg(argv[i] + 7, &out->port) != 0) return -1;
                continue;
            }
            if (strcmp(argv[i], "--bind") == 0) {
                if (i + 1 < argc) {
                    out->bind = argv[i + 1];
                    i++;
                } else {
                    snprintf(g_cli_error, sizeof g_cli_error, "option '--bind' requires a value (use '--bind=<addr>')");
                    return -1;
                }
                continue;
            }
            if (strncmp(argv[i], "--bind=", 7) == 0) {
                if (argv[i][7] == '\0') {
                    snprintf(g_cli_error, sizeof g_cli_error, "option '--bind' requires a value (use '--bind=<addr>')");
                    return -1;
                }
                out->bind = argv[i] + 7;
                continue;
            }
            if (strcmp(argv[i], "--webroot") == 0) {
                if (i + 1 < argc) {
                    out->webroot = argv[i + 1];
                    i++;
                } else {
                    snprintf(g_cli_error, sizeof g_cli_error, "option '--webroot' requires a value (use '--webroot=<dir>')");
                    return -1;
                }
                continue;
            }
            if (strncmp(argv[i], "--webroot=", 10) == 0) {
                if (argv[i][10] == '\0') {
                    snprintf(g_cli_error, sizeof g_cli_error, "option '--webroot' requires a value (use '--webroot=<dir>')");
                    return -1;
                }
                out->webroot = argv[i] + 10;
                continue;
            }
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
            /* 无子命令�? 第一个裸词视作子命令拼写错误 (�?"injec") */
            if (!out->op) {
                if (has_param_after(argc, argv, i + 1)) {
                    snprintf(g_cli_error, sizeof g_cli_error,
                             "missing subcommand before '%s'; try 'dcat inject %s ...' (available: inject, clean, query, list, serve)",
                             argv[i], argv[i]);
                } else {
                    snprintf(g_cli_error, sizeof g_cli_error,
                             "unknown subcommand '%s'; available: inject, clean, query, list, serve", argv[i]);
                }
                return -1;
            }
            /* 形如 key=value 但漏�?'--' 前缀 */
            const char *eq2 = strchr(argv[i], '=');
            if (eq2 && argv[i][0] != '-') {
                char k[64];
                size_t kl = (size_t)(eq2 - argv[i]);
                const char *vv = eq2 + 1;
                if (kl < sizeof(k)) {
                    memcpy(k, argv[i], kl); k[kl] = '\0';
                    snprintf(g_cli_error, sizeof g_cli_error,
                             "argument '%s' is missing the '--' prefix; did you mean '--%s=%s'?", argv[i], k, vv);
                } else {
                    snprintf(g_cli_error, sizeof g_cli_error,
                             "argument '%s' is missing the '--' prefix", argv[i]);
                }
            } else {
                snprintf(g_cli_error, sizeof g_cli_error,
                         "unexpected positional argument '%s'; expected --key=value (or --help/--config/--plugins)",
                         argv[i]);
            }
            return -1;
        }
        const char *kv = argv[i] + 2;
        if (kv[0] == '\0') {
            snprintf(g_cli_error, sizeof g_cli_error,
                     "empty parameter name in '%s'; expected --key=value", argv[i]);
            return -1;
        }
        const char *eq = strchr(kv, '=');
        if (!eq) {
            snprintf(g_cli_error, sizeof g_cli_error,
                     "parameter '%s' is missing '=value'; expected '--%s=<value>'", argv[i], kv);
            return -1;
        }
        char key[64];
        size_t kl = (size_t)(eq - kv);
        if (kl >= sizeof(key)) {
            snprintf(g_cli_error, sizeof g_cli_error,
                     "parameter name too long in '%s' (max %d)", argv[i], (int)(sizeof(key) - 1));
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
        if (g_cli_error[0] == '\0')
            snprintf(g_cli_error, sizeof g_cli_error,
                     "missing subcommand; available: inject, clean, query, list, serve");
        return -1;
    }
    return 0;
}
