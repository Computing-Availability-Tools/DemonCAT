#include "test.h"
#include "registry.h"
#include "config.h"
#include <string.h>

static config_t cfg;
static void setup(void) {
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
}

int test_find_and_list(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay");
    ASSERT_TRUE(f != NULL);
    ASSERT_STREQ(f->module, "network");
    ASSERT_TRUE(registry_find("nope") == NULL);
    int n = 0;
    registry_list(&n);
    ASSERT_INT_EQ(n, cfg.fault_count);
    ASSERT_INT_EQ(registry_count(), cfg.fault_count);
    return 0;
}

int main(void) {
    RUN_TEST(test_find_and_list);
    return TEST_MAIN_RETURN();
}
