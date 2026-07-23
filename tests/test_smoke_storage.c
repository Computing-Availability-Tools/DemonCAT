/* tests/test_smoke_storage.c — Tier 3: real execution tests for storage + port_occupy */
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

#define CK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", #cond); return 1; } } while (0)

static int count_proc(const char *name) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "pgrep -x '%s' 2>/dev/null | wc -l", name);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    int n = 0;
    fscanf(f, "%d", &n);
    pclose(f);
    return n;
}

static void smoke_setup(void) {
    config_t cfg;
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_init("/tmp/dcat_smoke_storage.json");
    state_load();
    executor_set_mock(NULL);
}

static void smoke_teardown(void) {
    state_init("");
    unlink("/tmp/dcat_smoke_storage.json");
    unlink("/tmp/dcat-rDISK_write_overload-*.pid");
    unlink("/tmp/dcat.write.*");
    unlink("/tmp/dcat-rNET_port_occupy-*.pid");
}

int main(void) {
    smoke_setup();

    /* ---- rDISK_write_overload (dd writers) ---- */
    {
        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "device"); strcpy(p.items[0].value, "/tmp"); p.count = 1;
        strcpy(p.items[1].key, "workers"); strcpy(p.items[1].value, "2"); p.count = 2;
        strcpy(p.items[2].key, "size_mb"); strcpy(p.items[2].value, "10"); p.count = 3;

        result_t *r = dispatch_inject("rDISK_write_overload", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        int n = count_proc("dd");
        CK(n >= 2);

        r = dispatch_clean("rDISK_write_overload", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        n = count_proc("dd if=/dev/zero");
        CK(n == 0);
    }

    /* ---- rNET_port_occupy (python3 socket holder) ---- */
    {
        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "port"); strcpy(p.items[0].value, "19999"); p.count = 1;

        result_t *r = dispatch_inject("rNET_port_occupy", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        /* check port is occupied */
        char cmd[128]; snprintf(cmd, sizeof cmd, "ss -tlnp 2>/dev/null | grep ':19999' | wc -l");
        FILE *f = popen(cmd, "r");
        CK(f);
        int n = 0; fscanf(f, "%d", &n); pclose(f);
        CK(n >= 1);

        r = dispatch_clean("rNET_port_occupy", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        snprintf(cmd, sizeof cmd, "ss -tlnp 2>/dev/null | grep ':19999' | wc -l");
        f = popen(cmd, "r");
        CK(f);
        n = 0; fscanf(f, "%d", &n); pclose(f);
        CK(n == 0);
    }

    smoke_teardown();
    printf("test_smoke_storage: 2 faults passed\n");
    return 0;
}
