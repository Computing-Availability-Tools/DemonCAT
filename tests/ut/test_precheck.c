#include "test.h"
#include "precheck.h"
#include "registry.h"
#include "config.h"
#include "output.h"
#include <string.h>

static config_t cfg;
static void setup(void) {
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
}

int test_precheck_uid_not_found(void) {
    setup();
    params_t p;
    params_init(&p);
    result_t *r = precheck(NULL, "inject", &p);
    ASSERT_INT_EQ(r->code, 4);
    result_free(r);
    return 0;
}

int test_precheck_op_not_supported(void) {
    setup();
    const fault_def_t *f = registry_find("rPROC_exit");
    params_t p;
    params_init(&p);
    params_set(&p, "pid", "1");
    result_t *r = precheck(f, "clean", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r);
    return 0;
}

int test_precheck_query_on_inject_only_rejected(void) {
    setup();
    const fault_def_t *f = registry_find("rPROC_exit");
    params_t p;
    params_init(&p);
    params_set(&p, "pid", "1");
    result_t *r = precheck(f, "query", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r);
    return 0;
}

int test_precheck_missing_required(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay");
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    result_t *r = precheck(f, "inject", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r);
    return 0;
}

int test_precheck_required_empty_rejected(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay");
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "");
    result_t *r = precheck(f, "inject", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r);
    return 0;
}

int test_precheck_undeclared_param_rejected(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay");
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");
    params_set(&p, "foo", "bar");
    result_t *r = precheck(f, "inject", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r);
    return 0;
}

int test_precheck_clean_no_required_check(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay");
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    result_t *r = precheck(f, "clean", &p);
    ASSERT_TRUE(r == NULL);
    result_free(r);
    return 0;
}

/* ---- 直接分支测:op_in_supported ---- */
int test_op_in_supported(void) {
    ASSERT_INT_EQ(op_in_supported("inject", "inject"), 1);
    ASSERT_INT_EQ(op_in_supported("inject,clean,query", "clean"), 1);
    ASSERT_INT_EQ(op_in_supported("inject,clean,query", "query"), 1);
    ASSERT_INT_EQ(op_in_supported("inject,clean,query", "inject"), 1);
    ASSERT_INT_EQ(op_in_supported("inject", "clean"), 0); /* 不在 */
    ASSERT_INT_EQ(op_in_supported("", "inject"), 0);      /* 空 */
    ASSERT_INT_EQ(op_in_supported(NULL, "inject"), 0);    /* NULL 防御 */
    return 0;
}

/* ---- 直接分支测:required_params_present ---- */
int test_required_params_present(void) {
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");

    ASSERT_INT_EQ(required_params_present("", &p), 1);                     /* 空必填 → 通过 */
    ASSERT_INT_EQ(required_params_present(NULL, &p), 1);                   /* NULL 防御 */
    ASSERT_INT_EQ(required_params_present("iface,loss_pct", &p), 1);       /* 全在 */
    ASSERT_INT_EQ(required_params_present("iface,loss_pct,extra", &p), 0); /* 缺 extra */

    params_t pe;
    params_init(&pe);
    params_set(&pe, "iface", ""); /* 空值 → 视为缺失(v[0]=='\0') */
    ASSERT_INT_EQ(required_params_present("iface", &pe), 0);
    return 0;
}

/* ---- 直接分支测:declared_params_only ---- */
int test_declared_params_only(void) {
    params_t p;
    params_init(&p);
    params_set(&p, "a", "1");
    params_set(&p, "b", "2");
    params_set(&p, "c", "3");

    /* a 在 inject_req; b 在 inject_opt; c 在 clean_req → 全声明 */
    ASSERT_INT_EQ(declared_params_only("a", "b", "c", "", "", "", &p), 1);
    /* a 在 query_opt(第 6 槽),仅含 a 的 params → 通过 */
    params_t pa;
    params_init(&pa);
    params_set(&pa, "a", "1");
    ASSERT_INT_EQ(declared_params_only("", "", "", "", "", "a", &pa), 1);
    /* b 未声明(仅 a 声明)→ 0,且首未声明为 b */
    ASSERT_INT_EQ(declared_params_only("a", "", "", "", "", "", &p), 0);
    ASSERT_STREQ(precheck_last_undeclared_param(), "b");

    /* 空 params → 直接通过 */
    params_t empty;
    params_init(&empty);
    ASSERT_INT_EQ(declared_params_only("", "", "", "", "", "", &empty), 1);
    return 0;
}

int main(void) {
    RUN_TEST(test_precheck_uid_not_found);
    RUN_TEST(test_precheck_op_not_supported);
    RUN_TEST(test_precheck_query_on_inject_only_rejected);
    RUN_TEST(test_precheck_missing_required);
    RUN_TEST(test_precheck_required_empty_rejected);
    RUN_TEST(test_precheck_undeclared_param_rejected);
    RUN_TEST(test_precheck_clean_no_required_check);
    RUN_TEST(test_op_in_supported);
    RUN_TEST(test_required_params_present);
    RUN_TEST(test_declared_params_only);
    return TEST_MAIN_RETURN();
}
