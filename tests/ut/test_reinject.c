/* tests/ut/test_reinject.c — TDD: 默认拒绝 + --force 原子替换 */
#include "test.h"
#include "reinject.h"
#include "dispatch.h"
#include "executor.h"
#include "registry.h"
#include "state.h"
#include "config.h"
#include "output.h"
#include <string.h>

static config_t g_cfg;
static result_t *mock_ok(const char *cmd, const char *const *env) {
    (void)cmd; (void)env;
    return result_ok("inject", "x", 0, "ok");
}
static void setup(void) {
    config_load("config/demoncat.conf", &g_cfg);
    registry_init(&g_cfg);
    state_reset();
    executor_set_mock(mock_ok);
}
static int bitset(const unsigned char *b, int n) { return (b[n / 8] >> (n % 8)) & 1; }

/* ---- cores_parse ---- */
int test_cores_parse_list_range_mixed_single(void) {
    unsigned char b[DCAT_CORES_BYTES];
    memset(b, 0, sizeof b);
    ASSERT_INT_EQ(cores_parse("0,1", b), 0);
    ASSERT_TRUE(bitset(b, 0) && bitset(b, 1) && !bitset(b, 2));

    memset(b, 0, sizeof b);
    ASSERT_INT_EQ(cores_parse("0-3", b), 0);
    ASSERT_TRUE(bitset(b, 0) && bitset(b, 1) && bitset(b, 2) && bitset(b, 3) && !bitset(b, 4));

    memset(b, 0, sizeof b);
    ASSERT_INT_EQ(cores_parse("0,1,4-6", b), 0);
    ASSERT_TRUE(bitset(b, 0) && bitset(b, 1) && !bitset(b, 2) && !bitset(b, 3)
                && bitset(b, 4) && bitset(b, 5) && bitset(b, 6) && !bitset(b, 7));

    memset(b, 0, sizeof b);
    ASSERT_INT_EQ(cores_parse("0", b), 0);
    ASSERT_TRUE(bitset(b, 0) && !bitset(b, 1));
    return 0;
}

int test_cores_parse_invalid(void) {
    unsigned char b[DCAT_CORES_BYTES];
    ASSERT_INT_EQ(cores_parse("", b), -1);
    ASSERT_INT_EQ(cores_parse("abc", b), -1);
    ASSERT_INT_EQ(cores_parse("0-2000", b), -1);        /* 越界 (≥DCAT_MAX_CORES) */
    ASSERT_INT_EQ(cores_parse("3-1", b), -1);           /* lo>hi */
    ASSERT_INT_EQ(cores_parse("0,1,", b), -1);          /* 尾随逗号 */
    ASSERT_INT_EQ(cores_parse("12345", b), -1);         /* 5 位 token → atoi 溢出防护 */
    return 0;
}

/* 大范围核集(如 640 核主机)可解析, 且与低位核正确判重叠。 */
int test_cores_parse_large_range_overlap(void) {
    unsigned char b[DCAT_CORES_BYTES];
    ASSERT_INT_EQ(cores_parse("0-155", b), 0);
    ASSERT_TRUE(bitset(b, 0) && bitset(b, 155) && !bitset(b, 156));

    unsigned char single[DCAT_CORES_BYTES];
    ASSERT_INT_EQ(cores_parse("0", single), 0);
    ASSERT_TRUE(cores_intersect(b, single));            /* {0..155} ∩ {0} = {0} */

    unsigned char hi[DCAT_CORES_BYTES];
    ASSERT_INT_EQ(cores_parse("300-400", hi), 0);
    ASSERT_TRUE(bitset(hi, 300) && bitset(hi, 400));
    ASSERT_TRUE(!cores_intersect(b, hi));               /* {0..155} ∩ {300..400} = ∅ */
    return 0;
}

/* ---- cores_intersect ---- */
int test_cores_intersect_overlap_disjoint(void) {
    unsigned char a[DCAT_CORES_BYTES], c[DCAT_CORES_BYTES];
    memset(a, 0, sizeof a); cores_parse("0,1", a);
    memset(c, 0, sizeof c); cores_parse("0,2", c);
    ASSERT_TRUE(cores_intersect(a, c));                /* 交集={0} */

    memset(c, 0, sizeof c); cores_parse("2,3", c);
    ASSERT_TRUE(!cores_intersect(a, c));                /* 交集=∅ */

    memset(c, 0, sizeof c); cores_parse("0-8", c);
    ASSERT_TRUE(cores_intersect(a, c));                 /* {0,1}∩{0..8} */
    return 0;
}

/* ---- reinject reject: CPU 同规格 ---- */
int test_reinject_reject_exact_same_cpu(void) {
    setup();
    params_t p; params_init(&p); params_set(&p, "cores", "0,1");
    result_t *r1 = dispatch_route_force("rCPU_overload", "inject", &p, 0);
    ASSERT_TRUE(r1 != NULL && r1->code == 0);
    result_free(r1);

    result_t *r2 = dispatch_route_force("rCPU_overload", "inject", &p, 0);
    ASSERT_TRUE(r2 != NULL);
    ASSERT_INT_EQ(r2->code, 5);                          /* reinject conflict */
    ASSERT_STR_CONTAINS(r2->json, "force");
    ASSERT_STR_CONTAINS(r2->json, "cores=0,1");          /* message 带出前次参数 */
    result_free(r2);
    return 0;
}

/* ---- reinject reject: CPU 核集重叠 ---- */
int test_reinject_reject_cores_overlap_set(void) {
    setup();
    params_t p1; params_init(&p1); params_set(&p1, "cores", "0,1");
    result_t *r1 = dispatch_route_force("rCPU_overload", "inject", &p1, 0);
    ASSERT_TRUE(r1 && r1->code == 0); result_free(r1);

    params_t p2; params_init(&p2); params_set(&p2, "cores", "0-8");
    result_t *r2 = dispatch_route_force("rCPU_overload", "inject", &p2, 0);
    ASSERT_INT_EQ(r2->code, 5);                          /* {0,1}∩{0..8} 重叠 */
    ASSERT_STR_CONTAINS(r2->json, "cores=0,1");          /* 前次注入参数 */
    result_free(r2);
    return 0;
}

/* ---- 不同资源(不重叠核)并发 OK ---- */
int test_reinject_different_cores_concurrent_ok(void) {
    setup();
    params_t p1; params_init(&p1); params_set(&p1, "cores", "0,1");
    ASSERT_TRUE(dispatch_route_force("rCPU_overload", "inject", &p1, 0)->code == 0);
    params_t p2; params_init(&p2); params_set(&p2, "cores", "2,3");
    result_t *r2 = dispatch_route_force("rCPU_overload", "inject", &p2, 0);
    ASSERT_TRUE(r2 && r2->code == 0); result_free(r2);
    ASSERT_INT_EQ(state_list_active(), 2);              /* 两条共存 */
    return 0;
}

/* ---- --force 原子替换 (CPU) ---- */
int test_reinject_force_replaces_cpu(void) {
    setup();
    params_t p1; params_init(&p1); params_set(&p1, "cores", "0,1");
    ASSERT_TRUE(dispatch_route_force("rCPU_overload", "inject", &p1, 0)->code == 0);

    params_t p2; params_init(&p2); params_set(&p2, "cores", "0-8");
    result_t *r = dispatch_route_force("rCPU_overload", "inject", &p2, 1);
    ASSERT_TRUE(r && r->code == 0); result_free(r);

    ASSERT_INT_EQ(state_list_active(), 1);              /* 旧 0,1 被清, 仅 0-8 */
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &p1, ids, DCAT_MAX_RECORDS), 0);
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &p2, ids, DCAT_MAX_RECORDS), 1);
    return 0;
}

/* ---- 网络标量: 同 iface reject, 不同 iface OK ---- */
int test_reinject_network_scalar_reject(void) {
    setup();
    params_t p1; params_init(&p1);
    params_set(&p1, "iface", "eth0"); params_set(&p1, "delay_ms", "100");
    ASSERT_TRUE(dispatch_route_force("rNET_delay", "inject", &p1, 0)->code == 0);

    params_t p2; params_init(&p2);
    params_set(&p2, "iface", "eth0"); params_set(&p2, "delay_ms", "200");
    result_t *r2 = dispatch_route_force("rNET_delay", "inject", &p2, 0);
    ASSERT_INT_EQ(r2->code, 5);                          /* 同 iface → 重叠 */
    ASSERT_STR_CONTAINS(r2->json, "iface=eth0");         /* 前次注入参数 */
    result_free(r2);

    params_t p3; params_init(&p3);
    params_set(&p3, "iface", "eth1"); params_set(&p3, "delay_ms", "200");
    result_t *r3 = dispatch_route_force("rNET_delay", "inject", &p3, 0);
    ASSERT_TRUE(r3 && r3->code == 0); result_free(r3);
    ASSERT_INT_EQ(state_list_active(), 2);
    return 0;
}

/* ---- 网络 --force 替换 ---- */
int test_reinject_network_force_replace(void) {
    setup();
    params_t p1; params_init(&p1);
    params_set(&p1, "iface", "eth0"); params_set(&p1, "delay_ms", "100");
    ASSERT_TRUE(dispatch_route_force("rNET_delay", "inject", &p1, 0)->code == 0);

    params_t p2; params_init(&p2);
    params_set(&p2, "iface", "eth0"); params_set(&p2, "delay_ms", "200");
    result_t *r = dispatch_route_force("rNET_delay", "inject", &p2, 1);
    ASSERT_TRUE(r && r->code == 0); result_free(r);

    ASSERT_INT_EQ(state_list_active(), 1);
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rNET_delay", &p2, ids, DCAT_MAX_RECORDS), 1);
    return 0;
}

/* ---- 多参资源键 AND (rNPU_arp: chip,dev,ip) ---- */
int test_reinject_multiparam_and_logic(void) {
    setup();
    params_t a; params_init(&a);
    params_set(&a, "chip", "0"); params_set(&a, "action", "add"); params_set(&a, "dev", "eth0");
    params_set(&a, "ip", "1.1.1.1"); params_set(&a, "mac", "de:ad:01");
    ASSERT_TRUE(dispatch_route_force("rNPU_arp", "inject", &a, 0)->code == 0);

    params_t b; params_init(&b);                        /* 不同 ip → 不同资源 */
    params_set(&b, "chip", "0"); params_set(&b, "action", "add"); params_set(&b, "dev", "eth0");
    params_set(&b, "ip", "2.2.2.2"); params_set(&b, "mac", "de:ad:02");
    ASSERT_TRUE(dispatch_route_force("rNPU_arp", "inject", &b, 0)->code == 0);

    params_t c; params_init(&c);                        /* 同 chip,dev,ip → 重叠 */
    params_set(&c, "chip", "0"); params_set(&c, "action", "add"); params_set(&c, "dev", "eth0");
    params_set(&c, "ip", "1.1.1.1"); params_set(&c, "mac", "de:ad:03");
    result_t *rc = dispatch_route_force("rNPU_arp", "inject", &c, 0);
    ASSERT_INT_EQ(rc->code, 5);
    ASSERT_STR_CONTAINS(rc->json, "ip=1.1.1.1");
    result_free(rc);

    result_t *rf = dispatch_route_force("rNPU_arp", "inject", &c, 1);  /* force 替换 */
    ASSERT_TRUE(rf && rf->code == 0); result_free(rf);
    ASSERT_INT_EQ(state_list_active(), 2);              /* b(2.2.2.2) + c(1.1.1.1) */
    return 0;
}

/* ---- inject-only 免检 (rPROC_exit: 不写 state → 0 overlap) ---- */
int test_reinject_inject_only_exempt(void) {
    setup();
    params_t p; params_init(&p); params_set(&p, "pid", "12345");
    result_t *r1 = dispatch_route_force("rPROC_exit", "inject", &p, 0);
    ASSERT_TRUE(r1 && r1->code == 0); result_free(r1);
    result_t *r2 = dispatch_route_force("rPROC_exit", "inject", &p, 0);  /* 不 reject */
    ASSERT_TRUE(r2 && r2->code == 0); result_free(r2);
    ASSERT_INT_EQ(state_list_active(), 0);               /* inject-only 不写 state */
    return 0;
}

/* ---- dispatch_route wrapper 后向兼容: 默认 force=0 → reject ---- */
int test_dispatch_route_wrapper_default_reject(void) {
    setup();
    params_t p; params_init(&p); params_set(&p, "cores", "0,1");
    ASSERT_TRUE(dispatch_route("rCPU_overload", "inject", &p)->code == 0);
    result_t *r2 = dispatch_route("rCPU_overload", "inject", &p);  /* wrapper force=0 */
    ASSERT_INT_EQ(r2->code, 5);                          /* 默认拒绝 */
    ASSERT_STR_CONTAINS(r2->json, "cores=0,1");          /* 前次注入参数 */
    result_free(r2);
    return 0;
}

static int g_clean_calls = 0;
static result_t *mock_fail_2nd_clean(const char *cmd, const char *const *env) {
    (void)cmd;
    const char *op = "inject";
    for (int i = 0; env && env[i]; i++)
        if (strncmp(env[i], "DCAT_OP=", 8) == 0) op = env[i] + 8;
    if (strcmp(op, "clean") == 0) {
        g_clean_calls++;
        if (g_clean_calls == 2) return result_err("clean", "x", 1, "simulated clean failure");
    }
    return result_ok("inject", "x", 0, "ok");
}

/* --force 清理多条重叠记录时中途 clean 失败: 已清的保持已清、中止注入、消息带 record id + 底层错误 */
int test_reinject_force_partial_clean_failure(void) {
    config_load("config/demoncat.conf", &g_cfg);
    registry_init(&g_cfg);
    state_reset();
    g_clean_calls = 0;
    executor_set_mock(mock_fail_2nd_clean);

    params_t p1; params_init(&p1); params_set(&p1, "cores", "0,1");
    ASSERT_TRUE(dispatch_route_force("rCPU_overload", "inject", &p1, 0)->code == 0);
    params_t p2; params_init(&p2); params_set(&p2, "cores", "2,3");
    ASSERT_TRUE(dispatch_route_force("rCPU_overload", "inject", &p2, 0)->code == 0);

    params_t p3; params_init(&p3); params_set(&p3, "cores", "0-3");
    result_t *r = dispatch_route_force("rCPU_overload", "inject", &p3, 1);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 1);                          /* --force clean 失败 */
    ASSERT_STR_CONTAINS(r->json, "record");             /* 消息指明哪条 record */
    ASSERT_STR_CONTAINS(r->json, "simulated clean failure"); /* 带出底层错误 */
    result_free(r);

    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &p1, ids, DCAT_MAX_RECORDS), 0); /* rec1 已清 */
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &p2, ids, DCAT_MAX_RECORDS), 1); /* rec2 仍活动 */
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &p3, ids, DCAT_MAX_RECORDS), 0); /* 未注入 */
    return 0;
}

int test_reinject_find_overlap_cpu(void) {
    setup();
    params_t p;
    params_init(&p);
    params_set(&p, "cores", "0,1");
    result_t *r1 = dispatch_route_force("rCPU_overload", "inject", &p, 0);
    ASSERT_TRUE(r1 && r1->code == 0);
    result_free(r1);
    const fault_def_t *f = registry_find("rCPU_overload");
    ASSERT_TRUE(f != NULL);
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(reinject_find_overlap(f, &p, ids, DCAT_MAX_RECORDS), 1);
    params_t q;
    params_init(&q);
    params_set(&q, "cores", "2,3");
    ASSERT_INT_EQ(reinject_find_overlap(f, &q, ids, DCAT_MAX_RECORDS), 0);
    ASSERT_INT_EQ(reinject_find_overlap(NULL, &p, ids, DCAT_MAX_RECORDS), 0);
    return 0;
}

int test_reinject_find_overlap_ops_generic(void) {
    setup();
    params_t p;
    params_init(&p);
    params_set(&p, "cores", "0,1");
    result_t *r = dispatch_route_force("rCPU_overload", "inject", &p, 0);
    ASSERT_TRUE(r && r->code == 0);
    result_free(r);
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(reinject_find_overlap_ops("rCPU_overload", "cores", &p, ids, DCAT_MAX_RECORDS), 1);
    params_t q;
    params_init(&q);
    params_set(&q, "cores", "5,6");
    ASSERT_INT_EQ(reinject_find_overlap_ops("rCPU_overload", "cores", &q, ids, DCAT_MAX_RECORDS), 0);
    ASSERT_INT_EQ(reinject_find_overlap_ops("rCPU_overload", "", &p, ids, DCAT_MAX_RECORDS), 1);
    params_t empty;
    params_init(&empty);
    ASSERT_INT_EQ(reinject_find_overlap_ops("rCPU_overload", "cores", &empty, ids, DCAT_MAX_RECORDS), 0);
    ASSERT_INT_EQ(reinject_find_overlap_ops(NULL, "cores", &p, ids, DCAT_MAX_RECORDS), 0);
    ASSERT_INT_EQ(reinject_find_overlap_ops("rCPU_overload", "cores", NULL, ids, DCAT_MAX_RECORDS), 0);
    return 0;
}

int main(void) {
    RUN_TEST(test_cores_parse_list_range_mixed_single);
    RUN_TEST(test_cores_parse_invalid);
    RUN_TEST(test_cores_parse_large_range_overlap);
    RUN_TEST(test_cores_intersect_overlap_disjoint);
    RUN_TEST(test_reinject_reject_exact_same_cpu);
    RUN_TEST(test_reinject_reject_cores_overlap_set);
    RUN_TEST(test_reinject_different_cores_concurrent_ok);
    RUN_TEST(test_reinject_force_replaces_cpu);
    RUN_TEST(test_reinject_network_scalar_reject);
    RUN_TEST(test_reinject_network_force_replace);
    RUN_TEST(test_reinject_multiparam_and_logic);
    RUN_TEST(test_reinject_inject_only_exempt);
    RUN_TEST(test_dispatch_route_wrapper_default_reject);
    RUN_TEST(test_reinject_force_partial_clean_failure);
    RUN_TEST(test_reinject_find_overlap_cpu);
    RUN_TEST(test_reinject_find_overlap_ops_generic);
    return TEST_MAIN_RETURN();
}
