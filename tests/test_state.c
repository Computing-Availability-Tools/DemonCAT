#include "test.h"
#include "state.h"
#include <string.h>

int test_add_find_by_params_list_inactive(void) {
    state_reset();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");
    int id = state_add("rNET_loss", &p);
    ASSERT_TRUE(id > 0);
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    int ids[DCAT_MAX_RECORDS]; int n = 0;
    n = state_find_by_params("rNET_loss", &q, ids, DCAT_MAX_RECORDS);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ids[0], id);
    const injection_record_t *r = state_find_by_id(id);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->active, 1);
    ASSERT_STREQ(params_find(&r->params, "iface"), "eth0");
    ASSERT_INT_EQ(state_list_active(), 1);
    state_mark_inactive(id);
    ASSERT_TRUE(state_find_by_id(id) == NULL || !state_find_by_id(id)->active);
    ASSERT_INT_EQ(state_list_active(), 0);
    return 0;
}

int test_concurrent_same_uid_diff_params(void) {
    state_reset();
    params_t p1; params_init(&p1); params_set(&p1, "iface", "eth0"); params_set(&p1, "loss_pct", "5");
    params_t p2; params_init(&p2); params_set(&p2, "iface", "eth1"); params_set(&p2, "loss_pct", "3");
    int id1 = state_add("rNET_loss", &p1);
    int id2 = state_add("rNET_loss", &p2);
    ASSERT_TRUE(id1 > 0 && id2 > 0 && id1 != id2);
    ASSERT_INT_EQ(state_list_active(), 2);
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    int ids[DCAT_MAX_RECORDS]; int n = 0;
    n = state_find_by_params("rNET_loss", &q, ids, DCAT_MAX_RECORDS);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ids[0], id1);
    state_mark_inactive(id1);
    ASSERT_INT_EQ(state_list_active(), 1);
    return 0;
}

int test_persistence_roundtrip(void) {
    state_reset();
    state_set_file("/tmp/dcat-test-state.json");
    params_t p; params_init(&p); params_set(&p, "iface", "eth0");
    int id = state_add("rNET_delay", &p);
    ASSERT_TRUE(id > 0);
    state_save();
    state_reset();
    state_load();
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    int ids[DCAT_MAX_RECORDS]; int n = 0;
    n = state_find_by_params("rNET_delay", &q, ids, DCAT_MAX_RECORDS);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ids[0], id);
    const injection_record_t *r = state_find_by_id(id);
    ASSERT_TRUE(r != NULL);
    ASSERT_STREQ(params_find(&r->params, "iface"), "eth0");
    return 0;
}

int main(void) {
    RUN_TEST(test_add_find_by_params_list_inactive);
    RUN_TEST(test_concurrent_same_uid_diff_params);
    RUN_TEST(test_persistence_roundtrip);
    return TEST_MAIN_RETURN();
}
