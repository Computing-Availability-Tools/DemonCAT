#include "test.h"
#include "plugins/plugin_manager.h"
#include "plugins/plugin.h"

int test_plugin_find_empty(void) {
    ASSERT_TRUE(plugin_find("nope") == NULL);
    ASSERT_INT_EQ(plugin_count(), 0);
    return 0;
}

int test_plugin_load_empty_dir(void) {
    int n = plugin_load_dir("/tmp/dcat-no-plugins-here-xyz");
    ASSERT_INT_EQ(n, 0);
    ASSERT_TRUE(plugin_find("nope") == NULL);
    ASSERT_INT_EQ(plugin_count(), 0);
    return 0;
}

int main(void) {
    RUN_TEST(test_plugin_find_empty);
    RUN_TEST(test_plugin_load_empty_dir);
    return TEST_MAIN_RETURN();
}
