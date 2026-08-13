#include "test.h"
#include "cli.h"
#include <string.h>

int test_parse_inject_flags(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "--iface=eth0", "--loss_pct=5"};
    parsed_cmd_t pc;
    int rc = cli_parse(5, (char **)argv, &pc);
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
    int rc = cli_parse(4, (char **)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "clean");
    ASSERT_STREQ(params_find(&pc.params, "iface"), "eth0");
    return 0;
}

int test_parse_query_no_uid(void) {
    const char *argv[] = {"dcat", "query"};
    parsed_cmd_t pc;
    int rc = cli_parse(2, (char **)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "query");
    ASSERT_TRUE(pc.uid[0] == '\0');
    return 0;
}

int test_parse_query_with_uid(void) {
    const char *argv[] = {"dcat", "query", "rCPU_overload", "--cores=2"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char **)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "query");
    ASSERT_STREQ(pc.uid, "rCPU_overload");
    ASSERT_STREQ(params_find(&pc.params, "cores"), "2");
    return 0;
}

int test_parse_list(void) {
    const char *argv[] = {"dcat", "list"};
    parsed_cmd_t pc;
    int rc = cli_parse(2, (char **)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "list");
    return 0;
}

int test_parse_global_options_excluded(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "--iface=eth0", "--config", "/x.conf"};
    parsed_cmd_t pc;
    int rc = cli_parse(6, (char **)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(params_find(&pc.params, "iface"), "eth0");
    ASSERT_TRUE(params_find(&pc.params, "config") == NULL);
    return 0;
}

int test_parse_unknown_subcommand(void) {
    const char *argv[] = {"dcat", "rPROC_exit"};
    parsed_cmd_t pc;
    int rc = cli_parse(2, (char **)argv, &pc);
    ASSERT_TRUE(rc != 0);
    ASSERT_STR_CONTAINS(cli_get_error(), "unknown subcommand 'rPROC_exit'");
    return 0;
}

int test_parse_missing_subcommand_with_param(void) {
    const char *argv[] = {"dcat", "rPROC_exit", "--pid=1"};
    parsed_cmd_t pc;
    int rc = cli_parse(3, (char **)argv, &pc);
    ASSERT_TRUE(rc != 0);
    ASSERT_STR_CONTAINS(cli_get_error(), "missing subcommand before 'rPROC_exit'");
    return 0;
}

int test_parse_unexpected_positional(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "extra"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char **)argv, &pc);
    ASSERT_TRUE(rc != 0);
    ASSERT_STR_CONTAINS(cli_get_error(), "unexpected positional argument 'extra'");
    return 0;
}

int test_parse_missing_dash_prefix(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "pid=1"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char **)argv, &pc);
    ASSERT_TRUE(rc != 0);
    ASSERT_STR_CONTAINS(cli_get_error(), "missing the '--' prefix");
    ASSERT_STR_CONTAINS(cli_get_error(), "did you mean '--pid=1'?");
    return 0;
}

int test_parse_missing_equal(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "--pid"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char **)argv, &pc);
    ASSERT_TRUE(rc != 0);
    ASSERT_STR_CONTAINS(cli_get_error(), "missing '=value'");
    return 0;
}

int test_parse_empty_key(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "--"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char **)argv, &pc);
    ASSERT_TRUE(rc != 0);
    ASSERT_STR_CONTAINS(cli_get_error(), "empty parameter name");
    return 0;
}

int test_parse_only_global_option(void) {
    const char *argv[] = {"dcat", "--config", "x.conf"};
    parsed_cmd_t pc;
    int rc = cli_parse(3, (char **)argv, &pc);
    ASSERT_TRUE(rc != 0);
    ASSERT_STR_CONTAINS(cli_get_error(), "missing subcommand");
    return 0;
}

int test_parse_force_flag(void) {
    const char *argv[] = {"dcat", "inject", "rCPU_overload", "--cores=0,1", "--force"};
    parsed_cmd_t pc;
    int rc = cli_parse(5, (char **)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "inject");
    ASSERT_STREQ(params_find(&pc.params, "cores"), "0,1");
    ASSERT_INT_EQ(pc.force, 1);
    return 0;
}

int test_parse_force_default_zero(void) {
    const char *argv[] = {"dcat", "inject", "rCPU_overload", "--cores=0,1"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char **)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(pc.force, 0);
    return 0;
}

int test_parse_force_with_value_error(void) {
    const char *argv[] = {"dcat", "inject", "rCPU_overload", "--cores=0,1", "--force=x"};
    parsed_cmd_t pc;
    int rc = cli_parse(5, (char **)argv, &pc);
    ASSERT_TRUE(rc != 0);
    ASSERT_STR_CONTAINS(cli_get_error(), "does not take a value");
    return 0;
}

int test_parse_force_on_clean_parsed(void) {
    const char *argv[] = {"dcat", "clean", "rNET_loss", "--iface=eth0", "--force"};
    parsed_cmd_t pc;
    int rc = cli_parse(5, (char **)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "clean");
    ASSERT_INT_EQ(pc.force, 1); /* 解析成功; dispatch 层忽略, 但 parse 不报错 */
    return 0;
}

int main(void) {
    RUN_TEST(test_parse_inject_flags);
    RUN_TEST(test_parse_clean_flags);
    RUN_TEST(test_parse_query_no_uid);
    RUN_TEST(test_parse_query_with_uid);
    RUN_TEST(test_parse_list);
    RUN_TEST(test_parse_global_options_excluded);
    RUN_TEST(test_parse_unknown_subcommand);
    RUN_TEST(test_parse_missing_subcommand_with_param);
    RUN_TEST(test_parse_unexpected_positional);
    RUN_TEST(test_parse_missing_dash_prefix);
    RUN_TEST(test_parse_missing_equal);
    RUN_TEST(test_parse_empty_key);
    RUN_TEST(test_parse_only_global_option);
    RUN_TEST(test_parse_force_flag);
    RUN_TEST(test_parse_force_default_zero);
    RUN_TEST(test_parse_force_with_value_error);
    RUN_TEST(test_parse_force_on_clean_parsed);
    return TEST_MAIN_RETURN();
}
