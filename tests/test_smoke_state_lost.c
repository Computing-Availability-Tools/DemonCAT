/* tests/test_smoke_state_lost.c — Tier 3: state.json 丢失后 stateless clean 仍能清除活跃故障。
 *
 * 场景：注入真实故障（写 /tmp sidecar + state.json）→ 模拟运维误删 state.json →
 *       分别走三条 clean 路径，验证故障确已从系统移除（进程恢复 + sidecar 消失）。
 *
 * 用 rPROC_hang（非 root、写 /tmp/dcat-rPROC_hang-<pid>.sidecar、可观测 State T/S）。
 */
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
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define CK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", #cond); return 1; } } while (0)

static const char *SFILE = "/tmp/dcat_smoke_state_lost.json";

static void setup(void) {
    config_t cfg;
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_reset();
    state_set_file(SFILE);
    state_load();                 /* 首次文件不存在 → state_is_lost()=1，内存空 */
    executor_set_mock(NULL);
    system("rm -f /tmp/dcat-rPROC_hang-*.sidecar 2>/dev/null");
}

static void teardown(void) {
    state_reset();
    state_set_file("");
    unlink(SFILE);
    system("rm -f /tmp/dcat-rPROC_hang-*.sidecar 2>/dev/null");
}

/* fork 一个 sleep 子进程并注入 rPROC_hang（SIGSTOP + 写 sidecar + 写 state）；
 * 返回子进程 pid，失败 -1。 */
static pid_t inject_hang(void) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { sleep(60); _exit(0); }
    char pidstr[16]; snprintf(pidstr, sizeof pidstr, "%d", pid);
    params_t p; memset(&p, 0, sizeof p);
    strcpy(p.items[0].key, "pid"); strcpy(p.items[0].value, pidstr); p.count = 1;
    result_t *r = dispatch_route("rPROC_hang", "inject", &p);
    int ok = (r && r->code == 0);
    result_free(r);
    return ok ? pid : -1;
}

static int is_stopped(pid_t pid) {
    char path[64]; snprintf(path, sizeof path, "/proc/%d/status", pid);
    FILE *f = fopen(path, "r"); if (!f) return -1;
    char line[256]; int stopped = 0;
    while (fgets(line, sizeof line, f))
        if (strstr(line, "State:") && strchr(line, 'T')) stopped = 1;
    fclose(f); return stopped;
}

static int sidecar_exists(pid_t pid) {
    char path[64]; snprintf(path, sizeof path, "/tmp/dcat-rPROC_hang-%d.sidecar", pid);
    return access(path, F_OK) == 0;
}

/* 模拟运维误删 state.json：删文件 + 重置内存 + 重新 load（文件缺失 → lost） */
static void lose_state(void) {
    unlink(SFILE);
    state_reset();
    state_set_file(SFILE);
    state_load();
}

/* kill+回收残留子进程，避免污染后续测试 */
static void reap(pid_t pid) {
    kill(pid, SIGCONT); kill(pid, SIGKILL); waitpid(pid, NULL, 0);
}

/* ---- Test 1: clean <uid> --params（state 丢失 → 回退用用户参数调脚本） ---- */
static int t_clean_with_params_after_state_lost(void) {
    setup();
    pid_t pid = inject_hang(); CK(pid > 0);
    sleep(1);
    CK(is_stopped(pid) == 1);          /* 故障在系统上生效 */
    CK(sidecar_exists(pid));           /* /tmp 工件存在 */

    lose_state();                      /* 误删 state.json */
    CK(state_is_lost());               /* 标记为丢失 */
    /* state 无记录 → 带参 clean 必须回退到脚本（用用户 pid） */
    char pidstr[16]; snprintf(pidstr, sizeof pidstr, "%d", pid);
    params_t p; memset(&p, 0, sizeof p);
    strcpy(p.items[0].key, "pid"); strcpy(p.items[0].value, pidstr); p.count = 1;
    result_t *r = dispatch_route("rPROC_hang", "clean", &p);
    CK(r && r->code == 0);
    result_free(r);
    sleep(1);
    CK(is_stopped(pid) == 0);          /* SIGCONT 恢复运行 */
    CK(!sidecar_exists(pid));          /* sidecar 已清 */
    reap(pid); teardown();
    return 0;
}

/* ---- Test 2: clean <uid>（无参，stateless glob /tmp 工件） ---- */
static int t_clean_no_params_after_state_lost(void) {
    setup();
    pid_t pid = inject_hang(); CK(pid > 0);
    sleep(1); CK(is_stopped(pid) == 1); CK(sidecar_exists(pid));
    lose_state(); CK(state_is_lost());
    params_t empty; memset(&empty, 0, sizeof empty);   /* count=0 */
    result_t *r = dispatch_route("rPROC_hang", "clean", &empty);
    CK(r && r->code == 0);
    result_free(r);
    sleep(1);
    CK(is_stopped(pid) == 0);
    CK(!sidecar_exists(pid));
    reap(pid); teardown();
    return 0;
}

/* ---- Test 3: clean --all（fan-out 无参 clean，stateless） ---- */
static int t_clean_all_after_state_lost(void) {
    setup();
    pid_t pid = inject_hang(); CK(pid > 0);
    sleep(1); CK(is_stopped(pid) == 1); CK(sidecar_exists(pid));
    lose_state(); CK(state_is_lost());
    result_t *r = dispatch_clean_all();
    CK(r && r->code == 0);
    result_free(r);
    sleep(1);
    CK(is_stopped(pid) == 0);
    CK(!sidecar_exists(pid));
    reap(pid); teardown();
    return 0;
}

/* ---- Test 4: 部分 state（文件有效但记录被抹除）→ 带参 clean 不回退（安全，不动系统资源），
 *              但无参 stateless clean 仍可恢复。锁定“仅在完全丢失才回退”的边界。 ---- */
static int t_clean_partial_state_safe_no_fallback(void) {
    setup();
    pid_t pid = inject_hang(); CK(pid > 0);
    sleep(1); CK(is_stopped(pid) == 1); CK(sidecar_exists(pid));

    /* 模拟部分损坏：把 state.json 重写为有效但空记录（运维手编辑/截断记录） */
    state_reset();                    /* 清内存记录，g_state_lost=0 */
    state_save();                      /* 覆盖为合法 JSON（空记录） */
    state_load();                      /* 文件合法 → state_is_lost()=0 */
    CK(!state_is_lost());

    char pidstr[16]; snprintf(pidstr, sizeof pidstr, "%d", pid);
    params_t p; memset(&p, 0, sizeof p);
    strcpy(p.items[0].key, "pid"); strcpy(p.items[0].value, pidstr); p.count = 1;
    result_t *r = dispatch_route("rPROC_hang", "clean", &p);
    CK(r && r->code != 0);           /* "no active injection"，不触碰进程 */
    result_free(r);
    sleep(1);
    CK(is_stopped(pid) == 1);         /* 仍处于 STOP（clean 未动它） */
    CK(sidecar_exists(pid));          /* sidecar 仍在 */

    /* 恢复手段：无参 stateless clean（glob /tmp 工件） */
    params_t empty; memset(&empty, 0, sizeof empty);
    result_t *r2 = dispatch_route("rPROC_hang", "clean", &empty);
    CK(r2 && r2->code == 0);
    result_free(r2);
    sleep(1);
    CK(is_stopped(pid) == 0);
    CK(!sidecar_exists(pid));
    reap(pid); teardown();
    return 0;
}

/* ---- Test 5: state 完好时无参 clean → 既清系统又 reconcile state（用户实测场景） ----
 * 注入 rPROC_hang（state 有记录）→ 无参 clean → 进程恢复 + sidecar 消失 +
 * state 记录被标 inactive（query 不再残留幽灵）。 */
static int t_clean_no_params_reconciles_state(void) {
    setup();
    pid_t pid = inject_hang(); CK(pid > 0);
    sleep(1); CK(is_stopped(pid) == 1); CK(sidecar_exists(pid));
    CK(state_list_active() == 1);       /* state 有该记录（区别于前 3 例误删后 state 为空） */
    params_t empty; memset(&empty, 0, sizeof empty);   /* count=0 */
    result_t *r = dispatch_route("rPROC_hang", "clean", &empty);
    CK(r && r->code == 0);
    result_free(r);
    sleep(1);
    CK(is_stopped(pid) == 0);            /* 系统层：SIGCONT 恢复 */
    CK(!sidecar_exists(pid));           /* 系统层：sidecar 已删 */
    /* state 层：记录应已 reconcile 为 inactive（无幽灵） */
    params_t q; memset(&q, 0, sizeof q);
    long long ids[DCAT_MAX_RECORDS];
    CK(state_find_by_params("rPROC_hang", &q, ids, DCAT_MAX_RECORDS) == 0);
    reap(pid); teardown();
    return 0;
}

int main(void) {
    int fail = 0;
    fprintf(stderr, "  -> t_clean_with_params_after_state_lost ... "); fflush(stderr);
    fail |= t_clean_with_params_after_state_lost(); fprintf(stderr, "%s\n", fail & 1 ? "FAIL" : "ok");
    fprintf(stderr, "  -> t_clean_no_params_after_state_lost ... "); fflush(stderr);
    { int r = t_clean_no_params_after_state_lost(); fail |= (r << 1); fprintf(stderr, "%s\n", r ? "FAIL" : "ok"); }
    fprintf(stderr, "  -> t_clean_all_after_state_lost ... "); fflush(stderr);
    { int r = t_clean_all_after_state_lost(); fail |= (r << 2); fprintf(stderr, "%s\n", r ? "FAIL" : "ok"); }
    fprintf(stderr, "  -> t_clean_partial_state_safe_no_fallback ... "); fflush(stderr);
    { int r = t_clean_partial_state_safe_no_fallback(); fail |= (r << 3); fprintf(stderr, "%s\n", r ? "FAIL" : "ok"); }
    fprintf(stderr, "  -> t_clean_no_params_reconciles_state ... "); fflush(stderr);
    { int r = t_clean_no_params_reconciles_state(); fail |= (r << 4); fprintf(stderr, "%s\n", r ? "FAIL" : "ok"); }
    return fail ? 1 : 0;
}
