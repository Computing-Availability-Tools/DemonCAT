#include "cli.h"
#include <string.h>
#include <stdlib.h>

static int is_subcommand(const char *s) {
    return strcmp(s, "inject") == 0 || strcmp(s, "clean") == 0 ||
           strcmp(s, "query") == 0 || strcmp(s, "list") == 0;
}

int cli_parse(int argc, char **argv, parsed_cmd_t *out) {
    memset(out, 0, sizeof(*out));
    params_init(&out->params);
    if (argc < 2) return -1;
    if (!is_subcommand(argv[1])) return -1;
    out->op = argv[1];
    int i = 2;
    if (i < argc && argv[i][0] != '-' && argv[i][0] != '\0' &&
        strcmp(argv[i], "values") != 0 && strcmp(argv[i], "where") != 0) {
        strncpy(out->uid, argv[i], sizeof(out->uid) - 1);
        out->uid[sizeof(out->uid) - 1] = '\0';
        i++;
    }
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "--help") == 0) {
            if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) i++;
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
