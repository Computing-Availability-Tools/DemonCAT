/* tests/ut/test_smoke_storage.c — Tier 3: real execution tests for storage + port_occupy */
#include "core/config.h"
#include "core/registry.h"
#include "core/state.h"
#include "core/executor.h"
#include "core/output.h"
#include "core/dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <glob.h>

#define CK(cond)                                  \
    do {                                          \
        if (!(cond)) {                            \
            fprintf(stderr, "FAIL: %s\n", #cond); \
            return 1;                             \
        }                                         \
    } while (0)

/* Count alive rDISK_write_overload worker subshells by scanning dcat pidfiles
 * (/tmp/dcat-rDISK_write_overload-<dev>.pid, space-separated PIDs per file) + kill -0 probe.
 * Immune to stray dd processes from other sources. */
static int count_writers(void) {
    glob_t g;
    if (glob("/tmp/dcat-rDISK_write_overload-*.pid", 0, NULL, &g) != 0) {
        globfree(&g);
        return 0;
    }
    int count = 0;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        FILE *f = fopen(g.gl_pathv[i], "r");
        if (!f) continue;
        int pid = 0;
        while (fscanf(f, "%d", &pid) == 1) {
            if (pid > 0 && kill(pid, 0) == 0) count++;
        }
        fclose(f);
    }
    globfree(&g);
    return count;
}

static void smoke_setup(void) {
    config_t cfg;
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_reset();
    state_set_file("/tmp/dcat_smoke_storage.json");
    state_load();
    executor_set_mock(NULL);
}

static void smoke_teardown(void) {
    state_reset();
    state_set_file("");
    unlink("/tmp/dcat_smoke_storage.json");
    /* unlink() 不支持 glob,需用 shell 通配清理 */
    int rc;
    rc = system("rm -f /tmp/dcat-rDISK_write_overload-*.pid");
    (void)rc;
    rc = system("rm -f /tmp/dcat.write.* /tmp/dcat.stress.*");
    (void)rc;
    rc = system("rm -f /tmp/dcat-rNET_port_occupy-*.pid");
    (void)rc;
}

int main(void) {
    smoke_setup();

    /* ---- rDISK_write_overload (dd writers) ---- */
    {
        params_t p;
        memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "device");
        strcpy(p.items[0].value, "/tmp");
        p.count = 1;
        strcpy(p.items[1].key, "workers");
        strcpy(p.items[1].value, "2");
        p.count = 2;
        strcpy(p.items[2].key, "size_mb");
        strcpy(p.items[2].value, "500");
        p.count = 3;

        result_t *r = dispatch_route("rDISK_write_overload", "inject", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(2);
        int n = count_writers();
        CK(n >= 2);

        r = dispatch_route("rDISK_write_overload", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        n = count_writers();
        CK(n == 0);
    }

    /* ---- rNET_port_occupy (python3 socket holder) ---- */
    {
        params_t p;
        memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "port");
        strcpy(p.items[0].value, "19999");
        p.count = 1;

        result_t *r = dispatch_route("rNET_port_occupy", "inject", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        /* check port is occupied */
        char cmd[128];
        snprintf(cmd, sizeof cmd, "ss -tlnp 2>/dev/null | grep ':19999' | wc -l");
        FILE *f = popen(cmd, "r");
        CK(f);
        int n = 0;
        if (fscanf(f, "%d", &n) != 1) n = 0;
        pclose(f);
        CK(n >= 1);

        r = dispatch_route("rNET_port_occupy", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        snprintf(cmd, sizeof cmd, "ss -tlnp 2>/dev/null | grep ':19999' | wc -l");
        f = popen(cmd, "r");
        CK(f);
        n = 0;
        if (fscanf(f, "%d", &n) != 1) n = 0;
        pclose(f);
        CK(n == 0);
    }

    smoke_teardown();
    printf("test_smoke_storage: 2 faults passed\n");
    return 0;
}
