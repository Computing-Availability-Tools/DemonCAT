#include "test.h"
#include "cli.h"
#include <string.h>

int test_parse_inject_flags(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "--iface=eth0", "--loss_pct=5"};
    parsed_cmd_t pc;
    int rc = cli_parse(5, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "inject");
    ASSERT_STREQ(pc.uid, "rNET_loss");
    ASSERT_STREQ(params_find(&pc.params, "iface"), "eth0");
    ASSERT_STREQ(params_find(&pc.params, "loss_pct"), "5");
    return 0;
}

int test_parse_clean_flags(void) {
    const char *argv[] = {"dcat", "clean", "rNET_loss", "--iface=eth0"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "clean");
    ASSERT_STREQ(params_find(&pc.params, "iface"), "eth0");
    return 0;
}

int test_parse_query_no_uid(void) {
    const char *argv[] = {"dcat", "query"};
    parsed_cmd_t pc;
    int rc = cli_parse(2, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "query");
    ASSERT_TRUE(pc.uid[0] == '\0');
    return 0;
}

int test_parse_query_with_uid(void) {
    const char *argv[] = {"dcat", "query", "rCPU_overload", "--cores=2"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "query");
    ASSERT_STREQ(pc.uid, "rCPU_overload");
    ASSERT_STREQ(params_find(&pc.params, "cores"), "2");
    return 0;
}

int test_parse_list(void) {
    const char *argv[] = {"dcat", "list"};
    parsed_cmd_t pc;
    int rc = cli_parse(2, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "list");
    return 0;
}

int test_parse_global_options_excluded(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "--iface=eth0", "--config", "/x.conf"};
    parsed_cmd_t pc;
    int rc = cli_parse(6, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(params_find(&pc.params, "iface"), "eth0");
    ASSERT_TRUE(params_find(&pc.params, "config") == NULL);
    return 0;
}

int main(void) {
    RUN_TEST(test_parse_inject_flags);
    RUN_TEST(test_parse_clean_flags);
    RUN_TEST(test_parse_query_no_uid);
    RUN_TEST(test_parse_query_with_uid);
    RUN_TEST(test_parse_list);
    RUN_TEST(test_parse_global_options_excluded);
    return TEST_MAIN_RETURN();
}
