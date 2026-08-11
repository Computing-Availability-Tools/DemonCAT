#include "test.h"
#include "help.h"
#include "registry.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>

static config_t g_cfg;
static void setup(void) { config_load("config/demoncat.conf", &g_cfg); registry_init(&g_cfg); }

int test_global_help(void) {
    char *t = help_render_global();
    ASSERT_TRUE(t != NULL);
    ASSERT_STR_CONTAINS(t, "usage:");
    ASSERT_STR_CONTAINS(t, "inject");
    ASSERT_STR_CONTAINS(t, "clean");
    ASSERT_STR_CONTAINS(t, "query");
    ASSERT_STR_CONTAINS(t, "list");
    free(t);
    return 0;
}

int test_inject_help_lists_faults(void) {
    setup();
    char *t = help_render_subcommand("inject", NULL);
    ASSERT_TRUE(t != NULL);
    ASSERT_STR_CONTAINS(t, "dcat inject");
    ASSERT_STR_CONTAINS(t, "rNET_loss");
    ASSERT_STR_CONTAINS(t, "iface,loss_pct");
    ASSERT_STR_CONTAINS(t, "rCPU_overload");
    free(t);
    return 0;
}

int test_clean_help_excludes_inject_only(void) {
    setup();
    char *t = help_render_subcommand("clean", NULL);
    ASSERT_TRUE(t != NULL);
    ASSERT_STR_CONTAINS(t, "rNET_loss");            /* 支持 clean */
    ASSERT_TRUE(strstr(t, "rPROC_exit") == NULL);   /* inject-only 不列�?*/
    free(t);
    return 0;
}

int test_uid_detail_and_example(void) {
    setup();
    char *t = help_render_subcommand("inject", "rNET_loss");
    ASSERT_TRUE(t != NULL);
    ASSERT_STR_CONTAINS(t, "Network packet loss");   /* desc */
    ASSERT_STR_CONTAINS(t, "dcat inject rNET_loss --iface=<iface> --loss_pct=<loss_pct>");
    free(t);
    return 0;
}

int test_clean_uid_example(void) {
    setup();
    char *t = help_render_subcommand("clean", "rNET_loss");
    ASSERT_TRUE(t != NULL);
    ASSERT_STR_CONTAINS(t, "dcat clean rNET_loss --iface=<iface>");
    free(t);
    return 0;
}

int test_query_help(void) {
    setup();
    char *t = help_render_subcommand("query", NULL);
    ASSERT_TRUE(t != NULL);
    ASSERT_STR_CONTAINS(t, "dcat query");
    ASSERT_STR_CONTAINS(t, "rNET_delay");
    ASSERT_STR_CONTAINS(t, "iface");   /* query 参数 iface（现归入 query_optional，不再必填） */
    free(t);
    return 0;
}

int test_list_help(void) {
    setup();
    char *t = help_render_subcommand("list", NULL);
    ASSERT_TRUE(t != NULL);
    ASSERT_STR_CONTAINS(t, "dcat list");
    ASSERT_STR_CONTAINS(t, "目录");
    free(t);
    return 0;
}

int test_unknown_uid(void) {
    setup();
    char *t = help_render_subcommand("inject", "nope");
    ASSERT_TRUE(t != NULL);
    ASSERT_STR_CONTAINS(t, "nope");
    ASSERT_STR_CONTAINS(t, "未知");
    free(t);
    return 0;
}

int main(void) {
    RUN_TEST(test_global_help);
    RUN_TEST(test_inject_help_lists_faults);
    RUN_TEST(test_clean_help_excludes_inject_only);
    RUN_TEST(test_uid_detail_and_example);
    RUN_TEST(test_clean_uid_example);
    RUN_TEST(test_query_help);
    RUN_TEST(test_list_help);
    RUN_TEST(test_unknown_uid);
    return TEST_MAIN_RETURN();
}
