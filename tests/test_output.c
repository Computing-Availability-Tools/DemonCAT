#include "test.h"
#include "output.h"
#include <string.h>

/* 成功（可恢复 inject）：含 record_id */
int test_ok_recoverable_has_record_id(void) {
    result_t *r = result_ok("inject", "rCPU_overload", 3, "ok");
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_STR_CONTAINS(r->json, "\"op\":\"inject\"");
    ASSERT_STR_CONTAINS(r->json, "\"uid\":\"rCPU_overload\"");
    ASSERT_STR_CONTAINS(r->json, "\"record_id\":3");
    ASSERT_STR_CONTAINS(r->json, "\"message\":\"ok\"");
    ASSERT_INT_EQ(r->code, 0);
    result_free(r);
    return 0;
}

/* 成功（inject-only）：无 record_id 字段 */
int test_ok_inject_only_no_record_id(void) {
    result_t *r = result_ok("inject", "rPROC_exit", 0, "killed");
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_TRUE(strstr(r->json, "record_id") == NULL);
    result_free(r);
    return 0;
}

/* 失败 */
int test_err(void) {
    result_t *r = result_err("inject", "rCPU_overload", 3, "missing required param: cores");
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"error\"");
    ASSERT_STR_CONTAINS(r->json, "\"code\":3");
    ASSERT_STR_CONTAINS(r->json, "missing required param");
    ASSERT_INT_EQ(r->code, 3);
    result_free(r);
    return 0;
}

int main(void) {
    RUN_TEST(test_ok_recoverable_has_record_id);
    RUN_TEST(test_ok_inject_only_no_record_id);
    RUN_TEST(test_err);
    return TEST_MAIN_RETURN();
}
