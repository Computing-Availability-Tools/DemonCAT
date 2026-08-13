#include "test.h"
#include "types.h"

int test_params_set_find(void) {
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");
    ASSERT_STREQ(params_find(&p, "iface"), "eth0");
    ASSERT_STREQ(params_find(&p, "loss_pct"), "5");
    ASSERT_TRUE(params_find(&p, "nope") == NULL);
    /* 覆盖更新 */
    params_set(&p, "iface", "eth1");
    ASSERT_STREQ(params_find(&p, "iface"), "eth1");
    return 0;
}

int test_params_count(void) {
    params_t p;
    params_init(&p);
    ASSERT_INT_EQ(p.count, 0);
    params_set(&p, "a", "1");
    params_set(&p, "b", "2");
    ASSERT_INT_EQ(p.count, 2);
    return 0;
}

int test_key_to_env(void) {
    /* DCAT_PARAM_<KEY>：非字母数字->'_'，大写 */
    ASSERT_STREQ(dcat_key_to_env("loss_pct"), "DCAT_PARAM_LOSS_PCT");
    ASSERT_STREQ(dcat_key_to_env("speed-mbps"), "DCAT_PARAM_SPEED_MBPS");
    ASSERT_STREQ(dcat_key_to_env("cores"), "DCAT_PARAM_CORES");
    return 0;
}

int test_params_equal_subset(void) {
    /* 用于 clean 按参数匹配：用户提供参数是记录参数的子集则匹配 */
    params_t rec;
    params_init(&rec);
    params_set(&rec, "iface", "eth0");
    params_set(&rec, "loss_pct", "5");
    params_t q;
    params_init(&q);
    params_set(&q, "iface", "eth0"); /* 子集 → 匹配 */
    ASSERT_TRUE(params_match_subset(&q, &rec));
    params_set(&q, "loss_pct", "5");
    ASSERT_TRUE(params_match_subset(&q, &rec)); /* 完全一致 → 匹配 */
    params_set(&q, "loss_pct", "3");            /* 值不同 → 不匹配 */
    ASSERT_TRUE(!params_match_subset(&q, &rec));
    params_t q2;
    params_init(&q2); /* 空 query 匹配所有 */
    ASSERT_TRUE(params_match_subset(&q2, &rec));
    return 0;
}

int test_params_set_overflow(void) {
    params_t p;
    params_init(&p);
    char key[8];
    for (int i = 0; i < DCAT_MAX_PARAMS; i++) {
        snprintf(key, sizeof key, "k%d", i);
        ASSERT_INT_EQ(params_set(&p, key, "v"), 0);
    }
    ASSERT_INT_EQ(p.count, DCAT_MAX_PARAMS);
    ASSERT_INT_EQ(params_set(&p, "extra", "v"), -1); /* 满返 -1 */
    ASSERT_INT_EQ(p.count, DCAT_MAX_PARAMS);         /* count 不变 */
    return 0;
}

int test_params_match_subset_key_missing(void) {
    /* query 的 key 不在 record 中:params_find 返回 NULL → 不匹配(覆盖 !v 分支) */
    params_t rec;
    params_init(&rec);
    params_set(&rec, "iface", "eth0");
    params_t q;
    params_init(&q);
    params_set(&q, "nope", "x");
    ASSERT_TRUE(!params_match_subset(&q, &rec));
    return 0;
}

int test_key_to_env_truncation(void) {
    /* buf[64],"DCAT_PARAM_" 11 字符前缀 → 最多 63 字符;超长 key 截断,且全为大写/_ */
    char longkey[128];
    memset(longkey, 'a', sizeof longkey - 1);
    longkey[sizeof longkey - 1] = '\0';
    const char *env = dcat_key_to_env(longkey);
    ASSERT_INT_EQ((int)strlen(env), 63);
    ASSERT_STR_CONTAINS(env, "DCAT_PARAM_");
    for (const char *c = env; *c; c++) {
        if (!((*c >= 'A' && *c <= 'Z') || *c == '_')) {
            fprintf(stderr, "unexpected char '%c' in env\n", *c);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    RUN_TEST(test_params_set_find);
    RUN_TEST(test_params_count);
    RUN_TEST(test_key_to_env);
    RUN_TEST(test_params_equal_subset);
    RUN_TEST(test_params_set_overflow);
    RUN_TEST(test_params_match_subset_key_missing);
    RUN_TEST(test_key_to_env_truncation);
    return TEST_MAIN_RETURN();
}
