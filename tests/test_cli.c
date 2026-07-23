/* tests/test_cli.c */
#include "core/cli.h"
#include <string.h>

static int eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

static int test_one(int argc, char **argv, const char *exp_op, const char *exp_uid,
                    int exp_pc, const char *k0, const char *v0, int expect_ok) {
    parsed_cmd_t out;
    int rc = cli_parse(argc, argv, &out);
    if (expect_ok) {
        if (rc != 0) return 1;
        if (!eq(out.op, exp_op)) return 1;
        if (!eq(out.uid, exp_uid)) return 1;
        if (out.params.count != exp_pc) return 1;
        if (exp_pc > 0 && (!eq(out.params.items[0].key, k0) || !eq(out.params.items[0].value, v0))) return 1;
    } else {
        if (rc == 0) return 1;
    }
    return 0;
}

int main(void) {
    /* inject with params */
    char *a1[] = {"dcat", "inject", "rCPU_overload", "--cores=4"};
    if (test_one(4, a1, "inject", "rCPU_overload", 1, "cores", "4", 1)) return 1;

    /* inject with multiple params */
    char *a2[] = {"dcat", "inject", "rNET_loss", "--iface=eth0", "--loss_pct=5"};
    if (test_one(5, a2, "inject", "rNET_loss", 2, "iface", "eth0", 1)) return 1;
    if (strcmp(a2[0] + 0, "dcat")) return 1; /* argv not modified */
    /* check second param */
    {
        parsed_cmd_t out;
        cli_parse(5, a2, &out);
        if (strcmp(out.params.items[1].key, "loss_pct") || strcmp(out.params.items[1].value, "5")) return 1;
    }

    /* clean with params */
    char *a3[] = {"dcat", "clean", "rNET_loss", "--iface=eth0"};
    if (test_one(4, a3, "clean", "rNET_loss", 1, "iface", "eth0", 1)) return 1;

    /* clean without params (clean all matching uid) */
    char *a4[] = {"dcat", "clean", "rNET_loss"};
    if (test_one(3, a4, "clean", "rNET_loss", 0, "", "", 1)) return 1;

    /* query with uid */
    char *a5[] = {"dcat", "query", "rCPU_overload"};
    if (test_one(3, a5, "query", "rCPU_overload", 0, "", "", 1)) return 1;

    /* query without uid */
    char *a6[] = {"dcat", "query"};
    if (test_one(2, a6, "query", "", 0, "", "", 1)) return 1;

    /* list (no uid, no params) */
    char *a7[] = {"dcat", "list"};
    if (test_one(2, a7, "list", "", 0, "", "", 1)) return 1;

    /* --config global option */
    char *a8[] = {"dcat", "inject", "rX", "--cores=1", "--config", "/tmp/conf"};
    {
        parsed_cmd_t out;
        if (cli_parse(6, a8, &out) != 0) return 1;
        if (!eq(out.op, "inject")) return 1;
        if (!eq(out.uid, "rX")) return 1;
        if (out.params.count != 1) return 1;
        if (strcmp(out.config_path, "/tmp/conf")) return 1;
    }

    /* --config before uid */
    char *a9[] = {"dcat", "--config", "/tmp/c", "inject", "rX", "--cores=1"};
    {
        parsed_cmd_t out;
        if (cli_parse(6, a9, &out) != 0) return 1;
        if (!eq(out.op, "inject")) return 1;
        if (strcmp(out.config_path, "/tmp/c")) return 1;
    }

    /* --help */
    char *a10[] = {"dcat", "--help"};
    {
        parsed_cmd_t out;
        if (cli_parse(2, a10, &out) != 0) return 1;
        if (!out.help) return 1;
    }

    /* inject-only fault inject (no clean param needed, just uid + param) */
    char *a11[] = {"dcat", "inject", "rPROC_exit", "--pid=12345"};
    if (test_one(4, a11, "inject", "rPROC_exit", 1, "pid", "12345", 1)) return 1;

    /* parse errors */
    char *b1[] = {"dcat"};                           /* no subcommand */
    if (test_one(1, b1, "", "", 0, "", "", 0)) return 1;
    char *b2[] = {"dcat", "frobnicate"};             /* unknown subcommand */
    if (test_one(2, b2, "", "", 0, "", "", 0)) return 1;
    char *b3[] = {"dcat", "inject"};                /* inject without uid */
    if (test_one(2, b3, "", "", 0, "", "", 0)) return 1;
    char *b4[] = {"dcat", "inject", "rX", "--badflag"}; /* --badflag without =value */
    if (test_one(4, b4, "", "", 0, "", "", 0)) return 1;

    return 0;
}
