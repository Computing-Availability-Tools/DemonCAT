#include "test.h"
#include "output.h"
#include <stdlib.h>
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

int test_result_raw(void) {
    result_t *r = result_raw("hello\n", 0);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 0);
    ASSERT_INT_EQ(r->raw, 1);
    ASSERT_STREQ(r->json, "hello\n");
    result_free(r);
    result_t *r2 = result_raw(NULL, 1);
    ASSERT_TRUE(r2 != NULL);
    ASSERT_INT_EQ(r2->code, 1);
    ASSERT_INT_EQ(r2->raw, 1);
    ASSERT_TRUE(r2->json == NULL);
    result_free(r2);
    return 0;
}

int test_output_to_json(void) {
    result_t *r = result_ok("inject", "rNET_loss", 3, "ok");
    char *s = output_to_json(r);
    ASSERT_TRUE(s != NULL);
    ASSERT_STR_CONTAINS(s, "timestamp");
    ASSERT_STR_CONTAINS(s, "\"status\":\"ok\"");
    ASSERT_STR_CONTAINS(s, "record_id");
    free(s);
    result_free(r);
    ASSERT_TRUE(output_to_json(NULL) == NULL);
    result_t empty;
    memset(&empty, 0, sizeof empty);
    ASSERT_TRUE(output_to_json(&empty) == NULL);
    result_t *rr = result_raw("plaintext not json", 0);
    char *ss = output_to_json(rr);
    ASSERT_TRUE(ss != NULL);
    ASSERT_STR_CONTAINS(ss, "plaintext not json");
    free(ss);
    result_free(rr);
    return 0;
}

int main(void) {
    RUN_TEST(test_ok_recoverable_has_record_id);
    RUN_TEST(test_ok_inject_only_no_record_id);
    RUN_TEST(test_err);
    RUN_TEST(test_result_raw);
    RUN_TEST(test_output_to_json);
    return TEST_MAIN_RETURN();
}
