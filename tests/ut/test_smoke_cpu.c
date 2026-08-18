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

#define CK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", #cond); return 1; } } while (0)

/* 找到两个相邻、且当前进程 affinity 允许的核 (a, a+1)。
 * 共享容器里会用 taskset/cpuset 屏蔽某些核(例如此环境 0,2-639 不含核1),
 * 对这些核 taskset -c 会 EINVAL, 故不能硬编码 "0,1"。失败返回 -1。 */
static int find_adjacent_cores(int *a) {
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof set, &set) != 0) return -1;
    int max = (int)sysconf(_SC_NPROCESSORS_CONF) + 64;
    for (int i = 0; i < max; i++) {
        if (CPU_ISSET(i, &set) && CPU_ISSET(i + 1, &set)) { *a = i; return 0; }
    }
    return -1;
}

/* Count alive rCPU_overload burn processes by scanning dcat pidfiles
 * (/tmp/dcat-rCPU_overload-c<core>.pid, one PID per file) + kill -0 probe.
 * Immune to stray perl from interrupted runs / serve / unrelated processes. */
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

/* 轮询等待 burn 进程数 >= min，最多等 timeout_sec 秒。
 * 代替固定 sleep(1)：系统高负载时 perl 启动可能慢于 1 秒。 */
static int wait_burn_min(int min, int timeout_sec) {
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (count_burn() >= min) return 1;
        usleep(100000);
    }
    return 0;
}

/* 轮询等待 burn 进程数 == 0，最多等 timeout_sec 秒。 */
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
        /* 连两个相邻可调度核都没有 → 环境无法支撑本测试, 明确报错 */
        fprintf(stderr, "FAIL: no two adjacent schedulable cores available\n");
        return 1;
    }
    char cores2[16], cores_range[16], core1[16];
    snprintf(cores2, sizeof cores2, "%d,%d", base, base + 1);
    snprintf(cores_range, sizeof cores_range, "%d-%d", base, base + 1);
    snprintf(core1, sizeof core1, "%d", base);

    /* ---- rCPU_overload (perl) ---- */
    {
        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "cores"); strcpy(p.items[0].value, cores2); p.count = 1;

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

    /* ---- rCPU_overload re-inject: 默认拒绝 + --force 原子替换 ---- */
    {
        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "cores"); strcpy(p.items[0].value, cores2); p.count = 1;

        result_t *r = dispatch_route_force("rCPU_overload", "inject", &p, 0);
        CK(r && r->code == 0); result_free(r);

        CK(wait_burn_min(2, 5));

        /* 同规格重注入: 默认拒绝 (code 5), 旧进程仍在 */
        r = dispatch_route_force("rCPU_overload", "inject", &p, 0);
        CK(r && r->code == 5); result_free(r);
        CK(wait_burn_min(2, 5));

        /* --force 原子替换: 旧清掉再注入, 不应翻倍 (<4) */
        r = dispatch_route_force("rCPU_overload", "inject", &p, 1);
        CK(r && r->code == 0); result_free(r);
        CK(wait_burn_min(2, 5));
        CK(count_burn() < 4);

        r = dispatch_route_force("rCPU_overload", "clean", &p, 0);
        CK(r && r->code == 0); result_free(r);

        CK(wait_burn_zero(5));
    }

    /* ---- rCPU_overload 重叠核集: 默认拒绝 + --force 替换 ---- */
    {
        params_t p1; memset(&p1, 0, sizeof p1);
        strcpy(p1.items[0].key, "cores"); strcpy(p1.items[0].value, cores_range); p1.count = 1;
        params_t p2; memset(&p2, 0, sizeof p2);
        strcpy(p2.items[0].key, "cores"); strcpy(p2.items[0].value, core1); p2.count = 1;

        result_t *r = dispatch_route_force("rCPU_overload", "inject", &p1, 0);
        CK(r && r->code == 0); result_free(r);

        CK(wait_burn_min(2, 5));

        /* 重叠核 base (含于 base-(base+1)): 默认拒绝 */
        r = dispatch_route_force("rCPU_overload", "inject", &p2, 0);
        CK(r && r->code == 5); result_free(r);

        /* --force 替换: base-(base+1) 清掉, 注入 base (perl 数从 2 降为 1) */
        r = dispatch_route_force("rCPU_overload", "inject", &p2, 1);
        CK(r && r->code == 0); result_free(r);
        CK(wait_burn_min(1, 5));
        CK(count_burn() < 3);

        r = dispatch_route_force("rCPU_overload", "clean", &p2, 0); CK(r && r->code == 0); result_free(r);

        CK(wait_burn_zero(5));
    }

    smoke_teardown();
    printf("test_smoke_cpu: 3 faults passed\n");
    return 0;
}
