#include "core/dispatch.h"
#include "core/cli.h"

#include <string.h>

static int ck(int cond) { return cond ? 0 : 1; }

int main(void) {
    char buf[512];

    /* --- Must quote "clean <uid>" and exe/cfgpath so the shell parses correctly --- */
    dispatch_build_reaper("/x/dcat", "rDISK_write_overload", "cfg.conf", 8, buf, sizeof buf);
    if (ck(strstr(buf, "\"clean rDISK_write_overload\"") != NULL)) return 1;
    if (ck(strstr(buf, "sleep 8;") != NULL)) return 1;
    if (ck(strstr(buf, "'/x/dcat'") != NULL)) return 1;            /* exe single-quoted (C6) */
    if (ck(strstr(buf, "--config 'cfg.conf'") != NULL)) return 1;  /* cfgpath single-quoted (C6) */
    if (ck(strstr(buf, "--yes") != NULL)) return 1;

    /* --- The quoted payload must actually PARSE as a valid clean command --- */
    const char *q = strstr(buf, "\"clean ");
    if (ck(q != NULL)) return 1;
    q += 1; /* skip opening quote */
    char payload[128];
    size_t k = 0;
    while (*q && *q != '"' && k + 1 < sizeof payload) payload[k++] = *q++;
    payload[k] = '\0';
    parsed_cmd_t pc;
    if (ck(cli_parse(payload, &pc) == 0)) return 1;
    if (ck(strcmp(pc.op, "clean") == 0)) return 1;
    if (ck(strcmp(pc.uid, "rDISK_write_overload") == 0)) return 1;

    /* --- NULL cfgpath omits --config entirely (C5 fix) — reaper's dcat finds
     * its own default config via /proc/self/exe --- */
    dispatch_build_reaper("/x/dcat", "rCPU_overload", NULL, 30, buf, sizeof buf);
    if (ck(strstr(buf, "\"clean rCPU_overload\"") != NULL)) return 1;
    if (ck(strstr(buf, "--config") == NULL)) return 1;   /* must NOT have --config */

    /* --- empty cfgpath also omits --config --- */
    dispatch_build_reaper("/x/dcat", "rCPU_overload", "", 30, buf, sizeof buf);
    if (ck(strstr(buf, "--config") == NULL)) return 1;   /* must NOT have --config */

    /* --- invalid inputs produce empty string (no crash) --- */
    dispatch_build_reaper(NULL, "rX", "c", 5, buf, sizeof buf);
    if (ck(buf[0] == '\0')) return 1;
    dispatch_build_reaper("/x/dcat", "rX", "c", 0, buf, sizeof buf);
    if (ck(buf[0] == '\0')) return 1;

    return 0;
}
