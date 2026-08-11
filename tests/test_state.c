#include "test.h"
#include "state.h"
#include <string.h>
#include <unistd.h>

int test_add_find_by_params_list_inactive(void) {
    state_reset();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");
    long long id = state_add("rNET_loss", &p);
    ASSERT_TRUE(id > 0);
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS]; int n = 0;
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
    long long id1 = state_add("rNET_loss", &p1);
    long long id2 = state_add("rNET_loss", &p2);
    ASSERT_TRUE(id1 > 0 && id2 > 0 && id1 != id2);
    ASSERT_INT_EQ(state_list_active(), 2);
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS]; int n = 0;
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
    long long id = state_add("rNET_delay", &p);
    ASSERT_TRUE(id > 0);
    const injection_record_t *r0 = state_find_by_id(id);
    ASSERT_TRUE(r0 != NULL);
    ASSERT_INT_EQ((long)strlen(r0->started_at), 19);
    ASSERT_TRUE(r0->started_at[4]=='-' && r0->started_at[7]=='-' && r0->started_at[10]==' '
                && r0->started_at[13]==':' && r0->started_at[16]==':');
    char saved[20]; strncpy(saved, r0->started_at, sizeof(saved)); saved[sizeof(saved)-1]='\0';
    state_save();
    state_reset();
    state_load();
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS]; int n = 0;
    n = state_find_by_params("rNET_delay", &q, ids, DCAT_MAX_RECORDS);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ids[0], id);
    const injection_record_t *r = state_find_by_id(id);
    ASSERT_TRUE(r != NULL);
    ASSERT_STREQ(params_find(&r->params, "iface"), "eth0");
    ASSERT_STREQ(r->started_at, saved);
    return 0;
}

int test_started_at_numeric_backcompat(void) {
    state_reset();
    state_set_file("/tmp/dcat-test-state-numeric.json");
    FILE *fp = fopen("/tmp/dcat-test-state-numeric.json", "w");
    ASSERT_TRUE(fp != NULL);
    fputs("{\"next_id\":1,\"records\":[{\"record_id\":1,\"uid\":\"rNET_delay\","
          "\"params\":{\"iface\":\"eth0\"},\"started_at\":1785398197,\"active\":true}]}", fp);
    fclose(fp);
    state_load();
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rNET_delay", &q, ids, DCAT_MAX_RECORDS), 1);
    const injection_record_t *r = state_find_by_id(1);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ((long)strlen(r->started_at), 19);
    ASSERT_TRUE(r->started_at[4]=='-' && r->started_at[7]=='-' && r->started_at[10]==' '
                && r->started_at[13]==':' && r->started_at[16]==':');
    unlink("/tmp/dcat-test-state-numeric.json");
    return 0;
}

int test_state_save_creates_missing_parent_dir(void) {
    state_reset();
    state_set_file("/tmp/dcat-fresh-state-test/sub/state.json");
    params_t p; params_init(&p); params_set(&p, "iface", "eth0");
    ASSERT_TRUE(state_add("rNET_delay", &p) > 0);
    state_save();
    state_reset();
    state_load();                                   /* 父目录不存在�?fopen 失败 �?load 找不�?*/
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rNET_delay", &p, ids, DCAT_MAX_RECORDS), 1);
    unlink("/tmp/dcat-fresh-state-test/sub/state.json");
    rmdir("/tmp/dcat-fresh-state-test/sub");
    rmdir("/tmp/dcat-fresh-state-test");
    return 0;
}

int test_state_load_missing_is_lost(void) {
    state_reset();
    state_set_file("/tmp/dcat-test-state-missing.json");
    state_load();
    ASSERT_TRUE(state_is_lost());
    ASSERT_INT_EQ(state_list_active(), 0);
    return 0;
}

int test_state_load_corrupt_is_lost(void) {
    state_reset();
    state_set_file("/tmp/dcat-test-state-corrupt.json");
    FILE *fp = fopen("/tmp/dcat-test-state-corrupt.json", "w");
    ASSERT_TRUE(fp != NULL);
    fputs("{not valid json}}}", fp);
    fclose(fp);
    state_load();
    ASSERT_TRUE(state_is_lost());
    ASSERT_INT_EQ(state_list_active(), 0);
    unlink("/tmp/dcat-test-state-corrupt.json");
    return 0;
}

int main(void) {
    RUN_TEST(test_add_find_by_params_list_inactive);
    RUN_TEST(test_concurrent_same_uid_diff_params);
    RUN_TEST(test_persistence_roundtrip);
    RUN_TEST(test_started_at_numeric_backcompat);
    RUN_TEST(test_state_load_missing_is_lost);
    RUN_TEST(test_state_load_corrupt_is_lost);
    RUN_TEST(test_state_save_creates_missing_parent_dir);
    return TEST_MAIN_RETURN();
}
