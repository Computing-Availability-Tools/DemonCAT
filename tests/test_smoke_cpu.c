/* tests/test_smoke_cpu.c — Tier 3: real execution tests for CPU faults */
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
    state_reset();
    state_set_file("/tmp/dcat_smoke_cpu.json");
    state_load();
    executor_set_mock(NULL);
}

static void smoke_teardown(void) {
    state_reset();
    state_set_file("");
    unlink("/tmp/dcat_smoke_cpu.json");
}

int main(void) {
    smoke_setup();

    /* ---- rCPU_overload (perl) ---- */
    {
        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "cores"); strcpy(p.items[0].value, "0,1"); p.count = 1;

        result_t *r = dispatch_route("rCPU_overload", "inject", &p);
        CK(r && r->code == 0);
        CK(strstr(r->json, "record_id") != NULL);
        result_free(r);

        sleep(1);
        int n = count_proc("perl");
        CK(n >= 2);

        r = dispatch_route("rCPU_overload", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        n = count_proc("perl");
        CK(n == 0);
    }

    /* ---- rCPU_overload re-inject: 默认拒绝 + --force 原子替换 ---- */
    {
        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "cores"); strcpy(p.items[0].value, "0,1"); p.count = 1;

        result_t *r = dispatch_route_force("rCPU_overload", "inject", &p, 0);
        CK(r && r->code == 0); result_free(r);

        sleep(1);
        CK(count_proc("perl") >= 2);

        /* 同规格重注入: 默认拒绝 (code 5), 旧进程仍在 */
        r = dispatch_route_force("rCPU_overload", "inject", &p, 0);
        CK(r && r->code == 5); result_free(r);
        sleep(1);
        CK(count_proc("perl") >= 2);

        /* --force 原子替换: 旧清掉再注入, 不应翻倍 (<4) */
        r = dispatch_route_force("rCPU_overload", "inject", &p, 1);
        CK(r && r->code == 0); result_free(r);
        sleep(1);
        int n = count_proc("perl");
        CK(n >= 2);
        CK(n < 4);

        r = dispatch_route_force("rCPU_overload", "clean", &p, 0);
        CK(r && r->code == 0); result_free(r);

        sleep(1);
        CK(count_proc("perl") == 0);
    }

    /* ---- rCPU_overload 重叠核集: 默认拒绝 + --force 替换 ---- */
    {
        params_t p1; memset(&p1, 0, sizeof p1);
        strcpy(p1.items[0].key, "cores"); strcpy(p1.items[0].value, "0-1"); p1.count = 1;
        params_t p2; memset(&p2, 0, sizeof p2);
        strcpy(p2.items[0].key, "cores"); strcpy(p2.items[0].value, "0"); p2.count = 1;

        result_t *r = dispatch_route_force("rCPU_overload", "inject", &p1, 0);
        CK(r && r->code == 0); result_free(r);

        sleep(1);
        CK(count_proc("perl") >= 2);

        /* 重叠核 0 (含于 0-1): 默认拒绝 */
        r = dispatch_route_force("rCPU_overload", "inject", &p2, 0);
        CK(r && r->code == 5); result_free(r);

        /* --force 替换: 0-1 清掉, 注入 0 (perl 数从 2 降为 1) */
        r = dispatch_route_force("rCPU_overload", "inject", &p2, 1);
        CK(r && r->code == 0); result_free(r);
        sleep(1);
        int n = count_proc("perl");
        CK(n >= 1);
        CK(n < 3);

        r = dispatch_route_force("rCPU_overload", "clean", &p2, 0); CK(r && r->code == 0); result_free(r);

        sleep(1);
        CK(count_proc("perl") == 0);
    }

    smoke_teardown();
    printf("test_smoke_cpu: 3 faults passed\n");
    return 0;
}
