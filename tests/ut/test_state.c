#include "test.h"
#include "state.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int test_add_find_by_params_list_inactive(void) {
    state_reset();
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");
    long long id = state_add("rNET_loss", &p);
    ASSERT_TRUE(id > 0);
    params_t q;
    params_init(&q);
    params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS];
    int n = 0;
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
    params_t p1;
    params_init(&p1);
    params_set(&p1, "iface", "eth0");
    params_set(&p1, "loss_pct", "5");
    params_t p2;
    params_init(&p2);
    params_set(&p2, "iface", "eth1");
    params_set(&p2, "loss_pct", "3");
    long long id1 = state_add("rNET_loss", &p1);
    long long id2 = state_add("rNET_loss", &p2);
    ASSERT_TRUE(id1 > 0 && id2 > 0 && id1 != id2);
    ASSERT_INT_EQ(state_list_active(), 2);
    params_t q;
    params_init(&q);
    params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS];
    int n = 0;
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
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    long long id = state_add("rNET_delay", &p);
    ASSERT_TRUE(id > 0);
    const injection_record_t *r0 = state_find_by_id(id);
    ASSERT_TRUE(r0 != NULL);
    ASSERT_INT_EQ((long)strlen(r0->started_at), 19);
    ASSERT_TRUE(r0->started_at[4] == '-' && r0->started_at[7] == '-' && r0->started_at[10] == ' ' && r0->started_at[13] == ':' && r0->started_at[16] == ':');
    char saved[20];
    strncpy(saved, r0->started_at, sizeof(saved));
    saved[sizeof(saved) - 1] = '\0';
    state_save();
    state_reset();
    state_load();
    params_t q;
    params_init(&q);
    params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS];
    int n = 0;
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
          "\"params\":{\"iface\":\"eth0\"},\"started_at\":1785398197,\"active\":true}]}",
          fp);
    fclose(fp);
    state_load();
    params_t q;
    params_init(&q);
    params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rNET_delay", &q, ids, DCAT_MAX_RECORDS), 1);
    const injection_record_t *r = state_find_by_id(1);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ((long)strlen(r->started_at), 19);
    ASSERT_TRUE(r->started_at[4] == '-' && r->started_at[7] == '-' && r->started_at[10] == ' ' && r->started_at[13] == ':' && r->started_at[16] == ':');
    unlink("/tmp/dcat-test-state-numeric.json");
    return 0;
}

int test_state_save_creates_missing_parent_dir(void) {
    state_reset();
    state_set_file("/tmp/dcat-fresh-state-test/sub/state.json");
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    ASSERT_TRUE(state_add("rNET_delay", &p) > 0);
    state_save();
    state_reset();
    state_load(); /* 父目录不存在时 fopen 失败 → load 找不到 */
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

static void visit_count(const injection_record_t *r, void *ctx) {
    (void)r;
    (*(int *)ctx)++;
}

int test_state_snapshot_by_uid(void) {
    state_reset();
    params_t p1;
    params_init(&p1);
    params_set(&p1, "iface", "eth0");
    params_t p2;
    params_init(&p2);
    params_set(&p2, "iface", "eth1");
    params_t p3;
    params_init(&p3);
    params_set(&p3, "pid", "1");
    long long id1 = state_add("rNET_delay", &p1);
    long long id2 = state_add("rNET_delay", &p2);
    long long id3 = state_add("rNET_loss", &p3);
    ASSERT_TRUE(id1 > 0 && id2 > 0 && id3 > 0);
    state_mark_inactive(id2);
    injection_record_t snap[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_snapshot_by_uid("rNET_delay", snap, DCAT_MAX_RECORDS), 1);
    ASSERT_INT_EQ(snap[0].record_id, id1);
    ASSERT_INT_EQ(snap[0].active, 1);
    ASSERT_STREQ(snap[0].uid, "rNET_delay");
    ASSERT_STREQ(params_find(&snap[0].params, "iface"), "eth0");
    ASSERT_INT_EQ(state_snapshot_by_uid("rCPU_overload", snap, DCAT_MAX_RECORDS), 0);
    ASSERT_INT_EQ(state_snapshot_by_uid("rNET_delay", snap, 0), 0);
    return 0;
}

int test_state_for_each_active(void) {
    state_reset();
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    long long id1 = state_add("rNET_delay", &p);
    long long id2 = state_add("rNET_loss", &p);
    long long id3 = state_add("rCPU_overload", &p);
    ASSERT_TRUE(id1 > 0 && id2 > 0 && id3 > 0);
    state_mark_inactive(id2);
    int cnt = 0;
    state_for_each_active(visit_count, &cnt);
    ASSERT_INT_EQ(cnt, 2);
    return 0;
}

int test_state_for_each_all(void) {
    state_reset();
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    long long id1 = state_add("rNET_delay", &p);
    long long id2 = state_add("rNET_loss", &p);
    long long id3 = state_add("rCPU_overload", &p);
    ASSERT_TRUE(id1 > 0 && id2 > 0 && id3 > 0);
    state_mark_inactive(id2);
    int cnt = 0;
    state_for_each_all(visit_count, &cnt);
    ASSERT_INT_EQ(cnt, 3);
    return 0;
}

/* C1 回归：跨进程并发 inject → state.json 丢记录。
 * 两进程必须都先把空文件 load 进内存（各自 next_id=1），再加各自记录后 save。
 * 屏障保证 load 全部完成后再放行 save：无文件锁时后写者覆盖先写者 → 丢 1 条。 */
int test_cross_process_concurrent_save_no_loss(void) {
    state_reset();
    const char *fp = "/tmp/dcat-test-state-cf.json";
    state_set_file(fp);
    unlink(fp);

    int ready_fds[2], go_fds[2]; /* ready: 子→父报"已load"; go: 父→子放行 save */
    ASSERT_INT_EQ(pipe(ready_fds), 0);
    ASSERT_INT_EQ(pipe(go_fds), 0);

    pid_t a = fork();
    ASSERT_TRUE(a >= 0);
    if (a == 0) {
        params_t pa;
        params_init(&pa);
        params_set(&pa, "iface", "eth0");
        state_load();
        char one = 'x';
        (void)write(ready_fds[1], &one, 1);
        (void)read(go_fds[0], &one, 1); /* 等父进程放行 */
        state_add("rNET_loss", &pa);
        state_save();
        _exit(0);
    }
    pid_t b = fork();
    ASSERT_TRUE(b >= 0);
    if (b == 0) {
        params_t pb;
        params_init(&pb);
        params_set(&pb, "iface", "eth1");
        state_load();
        char one = 'x';
        (void)write(ready_fds[1], &one, 1);
        (void)read(go_fds[0], &one, 1);
        state_add("rNET_delay", &pb);
        state_save();
        _exit(0);
    }
    close(ready_fds[1]);
    close(go_fds[0]);
    char c;
    (void)read(ready_fds[0], &c, 1); /* A 已 load */
    (void)read(ready_fds[0], &c, 1); /* B 已 load */
    char two[2] = {'g', 'g'};
    (void)write(go_fds[1], two, 2); /* 同时放行 A、B */
    close(ready_fds[0]);
    close(go_fds[1]);
    int st;
    waitpid(a, &st, 0);
    waitpid(b, &st, 0);

    /* 父进程恢复读盘：应同时看到 A、B 两条注入 */
    state_reset();
    state_load();
    params_t qa;
    params_init(&qa);
    params_set(&qa, "iface", "eth0");
    params_t qb;
    params_init(&qb);
    params_set(&qb, "iface", "eth1");
    long long ids[DCAT_MAX_RECORDS];
    int na = state_find_by_params("rNET_loss", &qa, ids, DCAT_MAX_RECORDS);
    int nb = state_find_by_params("rNET_delay", &qb, ids, DCAT_MAX_RECORDS);
    ASSERT_INT_EQ(na, 1);
    ASSERT_INT_EQ(nb, 1);
    unlink(fp);
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
    RUN_TEST(test_state_snapshot_by_uid);
    RUN_TEST(test_state_for_each_active);
    RUN_TEST(test_state_for_each_all);
    RUN_TEST(test_cross_process_concurrent_save_no_loss);
    return TEST_MAIN_RETURN();
}
