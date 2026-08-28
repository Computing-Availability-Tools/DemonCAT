/* tests/ut/test_smoke_cpu.c — Tier 3: real execution tests for CPU faults */
#define _GNU_SOURCE
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
#include <sched.h>

static const char *g_smoke_name = "";

#define CK(cond)                                                            \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s\n", #cond);                           \
            fprintf(stderr, "DCAT_SUBTEST|smoke|%s|FAIL|\n", g_smoke_name); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static int find_adjacent_cores(int *a) {
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof set, &set) != 0) return -1;
    int max = (int)sysconf(_SC_NPROCESSORS_CONF) + 64;
    for (int i = 0; i < max; i++) {
        if (CPU_ISSET(i, &set) && CPU_ISSET(i + 1, &set)) {
            *a = i;
            return 0;
        }
    }
    return -1;
}

static int count_burn(void) {
    glob_t g;
    if (glob("/tmp/dcat-rCPU_overload-c*.pid", 0, NULL, &g) != 0) {
        globfree(&g);
        return 0;
    }
    int count = 0;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        FILE *f = fopen(g.gl_pathv[i], "r");
        if (!f) continue;
        int pid = 0;
        if (fscanf(f, "%d", &pid) == 1 && pid > 0 && kill(pid, 0) == 0) count++;
        fclose(f);
    }
    globfree(&g);
    return count;
}

static int wait_burn_min(int min, int timeout_sec) {
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (count_burn() >= min) return 1;
        usleep(100000);
    }
    return 0;
}

static int wait_burn_zero(int timeout_sec) {
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (count_burn() == 0) return 1;
        usleep(100000);
    }
    return 0;
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

    int base;
    if (find_adjacent_cores(&base) != 0) {
        fprintf(stderr, "FAIL: no two adjacent schedulable cores available\n");
        fprintf(stderr, "DCAT_SUBTEST|smoke|rCPU_overload setup|FAIL|\n");
        return 1;
    }
    char cores2[24], cores_range[24], core1[16];
    snprintf(cores2, sizeof cores2, "%d,%d", base, base + 1);
    snprintf(cores_range, sizeof cores_range, "%d-%d", base, base + 1);
    snprintf(core1, sizeof core1, "%d", base);

    /* ---- rCPU_overload (perl) ---- */
    g_smoke_name = "rCPU_overload inject+clean";
    {
        params_t p;
        memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "cores");
        strcpy(p.items[0].value, cores2);
        p.count = 1;

        result_t *r = dispatch_route("rCPU_overload", "inject", &p);
        CK(r && r->code == 0);
        CK(strstr(r->json, "record_id") != NULL);
        result_free(r);

        CK(wait_burn_min(2, 5));

        r = dispatch_route("rCPU_overload", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);

        CK(wait_burn_zero(5));
    }
    fprintf(stderr, "DCAT_SUBTEST|smoke|%s|PASS|\n", g_smoke_name);

    /* ---- rCPU_overload re-inject: 默认拒绝 + --force 原子替换 ---- */
    g_smoke_name = "rCPU_overload reinject reject+force";
    {
        params_t p;
        memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "cores");
        strcpy(p.items[0].value, cores2);
        p.count = 1;

        result_t *r = dispatch_route_force("rCPU_overload", "inject", &p, 0);
        CK(r && r->code == 0);
        result_free(r);

        CK(wait_burn_min(2, 5));

        r = dispatch_route_force("rCPU_overload", "inject", &p, 0);
        CK(r && r->code == 5);
        result_free(r);
        CK(wait_burn_min(2, 5));

        r = dispatch_route_force("rCPU_overload", "inject", &p, 1);
        CK(r && r->code == 0);
        result_free(r);
        CK(wait_burn_min(2, 5));
        CK(count_burn() < 4);

        r = dispatch_route_force("rCPU_overload", "clean", &p, 0);
        CK(r && r->code == 0);
        result_free(r);

        CK(wait_burn_zero(5));
    }
    fprintf(stderr, "DCAT_SUBTEST|smoke|%s|PASS|\n", g_smoke_name);

    /* ---- rCPU_overload 重叠核集: 默认拒绝 + --force 替换 ---- */
    g_smoke_name = "rCPU_overload overlap reject+force";
    {
        params_t p1;
        memset(&p1, 0, sizeof p1);
        strcpy(p1.items[0].key, "cores");
        strcpy(p1.items[0].value, cores_range);
        p1.count = 1;
        params_t p2;
        memset(&p2, 0, sizeof p2);
        strcpy(p2.items[0].key, "cores");
        strcpy(p2.items[0].value, core1);
        p2.count = 1;

        result_t *r = dispatch_route_force("rCPU_overload", "inject", &p1, 0);
        CK(r && r->code == 0);
        result_free(r);

        CK(wait_burn_min(2, 5));

        r = dispatch_route_force("rCPU_overload", "inject", &p2, 0);
        CK(r && r->code == 5);
        result_free(r);

        r = dispatch_route_force("rCPU_overload", "inject", &p2, 1);
        CK(r && r->code == 0);
        result_free(r);
        CK(wait_burn_min(1, 5));
        CK(count_burn() < 3);

        r = dispatch_route_force("rCPU_overload", "clean", &p2, 0);
        CK(r && r->code == 0);
        result_free(r);

        CK(wait_burn_zero(5));
    }
    fprintf(stderr, "DCAT_SUBTEST|smoke|%s|PASS|\n", g_smoke_name);

    smoke_teardown();
    return 0;
}
