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

int main(void) {
    RUN_TEST(test_precheck_uid_not_found);
    RUN_TEST(test_precheck_op_not_supported);
    RUN_TEST(test_precheck_query_on_inject_only_rejected);
    RUN_TEST(test_precheck_missing_required);
    RUN_TEST(test_precheck_required_empty_rejected);
    RUN_TEST(test_precheck_undeclared_param_rejected);
    RUN_TEST(test_precheck_clean_no_required_check);
    return TEST_MAIN_RETURN();
}
