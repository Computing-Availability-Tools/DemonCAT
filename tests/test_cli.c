#include "core/cli.h"
#include <string.h>

static int eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

int main(void) {
    struct { const char *in; const char *op; const char *uid; int pc; const char *k0; const char *v0; int ok; } cs[] = {
        {"inject rCPU_overload (cores,duration) values (4,60)", "inject", "rCPU_overload", 2, "cores", "4", 1},
        {"clean rNET_delay where iface=eth0", "clean", "rNET_delay", 1, "iface", "eth0", 1},
        {"list", "list", "", 0, "", "", 1},
        {"query rCPU_overload", "query", "rCPU_overload", 0, "", "", 1},
        {"query", "query", "", 0, "", "", 1},
        {"clean rNET_delay where iface=eth0 delay_ms=100", "clean", "rNET_delay", 2, "iface", "eth0", 1},
    };
    int fails = 0;
    for (size_t i = 0; i < sizeof cs / sizeof *cs; i++) {
        parsed_cmd_t out;
        int rc = cli_parse(cs[i].in, &out);
        if (cs[i].ok) {
            if (rc != 0) { fails++; continue; }
            if (!eq(out.op, cs[i].op) || !eq(out.uid, cs[i].uid) || out.params.count != cs[i].pc) { fails++; continue; }
            if (cs[i].pc > 0 && (!eq(out.params.items[0].key, cs[i].k0) || !eq(out.params.items[0].value, cs[i].v0))) fails++;
        } else {
            if (rc == 0) fails++;
        }
    }
    const char *bad[] = {"inject", "frobnicate x", "inject rX (a) values (1,2)", "clean where x=1", "", "inject rX extra junk"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        parsed_cmd_t e;
        if (cli_parse(bad[i], &e) == 0) fails++;
    }
    return fails ? 1 : 0;
}
