#include "test.h"
#include "config.h"
#include <string.h>

int test_load_faults(void) {
    config_t cfg;
    int rc = config_load("config/demoncat.conf", &cfg);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.fault_count, 37);
    const fault_def_t *f = config_find(&cfg, "rNET_delay");
    ASSERT_TRUE(f != NULL);
    ASSERT_STREQ(f->module, "network");
    ASSERT_STREQ(f->supported_ops, "inject,clean,query");
    ASSERT_STR_CONTAINS(f->inject_required, "iface");
    ASSERT_STR_CONTAINS(f->inject_required, "delay_ms");
    ASSERT_STR_CONTAINS(f->clean_required, "iface");
    ASSERT_STR_CONTAINS(f->query_required, "iface");
    /* inject-only: clean/query fields empty */
    const fault_def_t *p = config_find(&cfg, "rPROC_exit");
    ASSERT_TRUE(p != NULL);
    ASSERT_STREQ(p->supported_ops, "inject");
    ASSERT_STR_CONTAINS(p->inject_required, "pid");
    ASSERT_STREQ(p->clean_required, "");
    ASSERT_STREQ(p->query_required, "");
    ASSERT_TRUE(config_find(&cfg, "nope") == NULL);
    return 0;
}

int test_resolve_script(void) {
    char dst[512];
    resolve_script("/opt/dcat", "scripts/network/net_delay.sh", dst, sizeof(dst));
    ASSERT_STREQ(dst, "/opt/dcat/scripts/network/net_delay.sh");
    resolve_script("/opt/dcat", "/abs/path.sh", dst, sizeof(dst));
    ASSERT_STREQ(dst, "/abs/path.sh");            /* 绝对路径不变 */
    resolve_script("/opt/dcat", "~/x.sh", dst, sizeof(dst));
    ASSERT_STREQ(dst, "~/x.sh");                 /* home 相对不变 */
    resolve_script(".", "scripts/x.sh", dst, sizeof(dst));
    ASSERT_STREQ(dst, "scripts/x.sh");           /* root='.' 走 CWD */
    return 0;
}

int test_derive_project_root(void) {
    char root[256];
    derive_project_root("/opt/dcat/config/demoncat.conf", root, sizeof(root));
    ASSERT_STREQ(root, "/opt/dcat");
    derive_project_root("config/demoncat.conf", root, sizeof(root));
    ASSERT_STREQ(root, ".");                      /* 相对配置 → root='.' */
    return 0;
}

int main(void) {
    RUN_TEST(test_load_faults);
    RUN_TEST(test_resolve_script);
    RUN_TEST(test_derive_project_root);
    return TEST_MAIN_RETURN();
}
