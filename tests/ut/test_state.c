#include "test.h"
#include "state.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

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
    unlink("/tmp/dcat-test-state.json"); /* 独立用例：避免历史文件累积 */
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
        ssize_t w1 = write(ready_fds[1], &one, 1);
        ssize_t r1 = read(go_fds[0], &one, 1); /* 等父进程放行 */
        (void)w1;
        (void)r1;
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
        ssize_t w1 = write(ready_fds[1], &one, 1);
        ssize_t r1 = read(go_fds[0], &one, 1);
        (void)w1;
        (void)r1;
        state_add("rNET_delay", &pb);
        state_save();
        _exit(0);
    }
    close(ready_fds[1]);
    close(go_fds[0]);
    char c;
    ssize_t rr1 = read(ready_fds[0], &c, 1); /* A 已 load */
    ssize_t rr2 = read(ready_fds[0], &c, 1); /* B 已 load */
    (void)rr1;
    (void)rr2;
    char two[2] = {'g', 'g'};
    ssize_t wg = write(go_fds[1], two, 2); /* 同时放行 A、B */
    (void)wg;
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

/* 回归：两个进程从空 state 并发注入【同 uid + 同参数】时，两条都必须保留。
 * 修复前第一遍精确匹配只比 record_id+uid+params，B 的 id=1 会被 A 的 id=1 合并掉。 */
int test_concurrent_same_params_no_loss(void) {
    state_reset();
    const char *fp = "/tmp/dcat-test-state-dup-conc.json";
    state_set_file(fp);
    unlink(fp);

    int ready_fds[2], go_fds[2];
    ASSERT_INT_EQ(pipe(ready_fds), 0);
    ASSERT_INT_EQ(pipe(go_fds), 0);

    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");

    pid_t a = fork();
    ASSERT_TRUE(a >= 0);
    if (a == 0) {
        close(ready_fds[0]);
        close(go_fds[1]);
        state_load();
        state_add("rNET_delay", &p);
        char c = 'x';
        ssize_t w = write(ready_fds[1], &c, 1);
        ssize_t r = read(go_fds[0], &c, 1);
        (void)w;
        (void)r;
        state_save();
        _exit(0);
    }
    pid_t b = fork();
    ASSERT_TRUE(b >= 0);
    if (b == 0) {
        close(ready_fds[0]);
        close(go_fds[1]);
        state_load();
        state_add("rNET_delay", &p);
        char c = 'x';
        ssize_t w = write(ready_fds[1], &c, 1);
        ssize_t r = read(go_fds[0], &c, 1);
        (void)w;
        (void)r;
        state_save();
        _exit(0);
    }
    close(ready_fds[1]);
    close(go_fds[0]);
    char c;
    ssize_t rr1 = read(ready_fds[0], &c, 1);
    ssize_t rr2 = read(ready_fds[0], &c, 1);
    (void)rr1;
    (void)rr2;
    char two[2] = {'g', 'g'};
    ssize_t wg = write(go_fds[1], two, 2);
    (void)wg;
    close(ready_fds[0]);
    close(go_fds[1]);
    int st;
    waitpid(a, &st, 0);
    waitpid(b, &st, 0);

    state_reset();
    state_load();
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rNET_delay", &p, ids, DCAT_MAX_RECORDS), 2);
    ASSERT_INT_EQ(state_list_active(), 2);
    unlink(fp);
    return 0;
}

/* W1 回归：并发注入重编号后，内存持旧 id 的记录执行 clean+save 不得产生 ghost，
 * 且真实磁盘记录应被清 inactive。
 * 序列：A、B 都从空盘 load(next=1)→add（均得 id=1）；A 先 save 落盘 loss@1；
 *       B save 时 delay@1 与 loss@1 撞 id → 重编号 delay@2 写盘，但 B 内存仍 delay@1；
 *       B 再 mark_inactive(1)+save。
 * 修复前：merge 用 record_id+uid+params 匹配，delay@1(磁盘是 delay@2)匹配不上
 *         → 当新记录 id@3 追加(ghost)，磁盘 delay@2 仍 active → clean 失效。
 * 修复后：按 uid+params 且磁盘 active 匹配 → delay@1 命中磁盘 delay@2，只置 inactive。 */
struct delay_count_ctx {
    int cnt;
    int active_cnt;
};

static void visit_count_delay(const injection_record_t *r, void *ctx) {
    struct delay_count_ctx *v = (struct delay_count_ctx *)ctx;
    if (strcmp(r->uid, "rNET_delay") == 0) {
        v->cnt++;
        if (r->active) v->active_cnt++;
    }
}

/* W3 visitor：统计 rNET_delay 各 delay_ms 计数的 active/inactive */
struct w3_ctx {
    int cnt50, act50, cnt80, act80;
    long long found_id;
};

static void visit_w3(const injection_record_t *r, void *ctx) {
    struct w3_ctx *w = (struct w3_ctx *)ctx;
    if (strcmp(r->uid, "rNET_delay") != 0) return;
    const char *iface = params_find(&r->params, "iface");
    if (!iface || strcmp(iface, "eth9") != 0) return;
    const char *dm = params_find(&r->params, "delay_ms");
    if (dm && strcmp(dm, "50") == 0) {
        w->cnt50++;
        if (r->active) {
            w->act50++;
            if (r->record_id > w->found_id) w->found_id = r->record_id;
        }
    } else if (dm && strcmp(dm, "80") == 0) {
        w->cnt80++;
        if (r->active) w->act80++;
    }
}

int test_clean_after_renumber_no_ghost(void) {
    state_reset();
    const char *fp = "/tmp/dcat-test-state-w1.json";
    state_set_file(fp);
    unlink(fp);

    /* 每进程独立 go/ack 管道：共享管道会让两个子进程竞争同一字节 → waitpid 挂死 */
    int ready_fds[2], goa[2], gob1[2], gob2[2], acka[2], ackb[2];
    ASSERT_INT_EQ(pipe(ready_fds), 0);
    ASSERT_INT_EQ(pipe(goa), 0);
    ASSERT_INT_EQ(pipe(gob1), 0);
    ASSERT_INT_EQ(pipe(gob2), 0);
    ASSERT_INT_EQ(pipe(acka), 0);
    ASSERT_INT_EQ(pipe(ackb), 0);

    pid_t a = fork();
    ASSERT_TRUE(a >= 0);
    if (a == 0) {
        params_t pa;
        params_init(&pa);
        params_set(&pa, "iface", "eth0");
        close(ready_fds[0]);
        close(goa[1]);
        close(gob1[0]);
        close(gob1[1]);
        close(gob2[0]);
        close(gob2[1]);
        close(acka[0]);
        close(ackb[0]);
        close(ackb[1]);
        state_load();
        state_add("rNET_loss", &pa); /* id=1 */
        char one = 'x';
        ssize_t w1 = write(ready_fds[1], &one, 1);
        ssize_t r1 = read(goa[0], &one, 1); /* 放行 A save */
        (void)w1;
        (void)r1;
        state_save();
        ssize_t w2 = write(acka[1], &one, 1);
        (void)w2;
        _exit(0);
    }
    pid_t b = fork();
    ASSERT_TRUE(b >= 0);
    if (b == 0) {
        params_t pb;
        params_init(&pb);
        params_set(&pb, "iface", "eth1");
        close(ready_fds[0]);
        close(goa[0]);
        close(goa[1]);
        close(gob1[1]);
        close(gob2[1]);
        close(acka[0]);
        close(acka[1]);
        close(ackb[0]);
        state_load();
        long long bid = state_add("rNET_delay", &pb); /* id=1 */
        char one = 'x';
        ssize_t w1 = write(ready_fds[1], &one, 1);
        ssize_t r1 = read(gob1[0], &one, 1); /* 放行 B save */
        (void)w1;
        (void)r1;
        state_save(); /* delay@1 与磁盘 loss@1 撞 id → 重编号 delay@2 写盘 */
        ssize_t w2 = write(ackb[1], &one, 1);
        (void)w2;
        ssize_t r2 = read(gob2[0], &one, 1); /* 放行 B clean */
        (void)r2;
        state_mark_inactive(bid); /* 内存仍持旧 id=1 */
        state_save();
        _exit(0);
    }
    close(ready_fds[1]);
    close(goa[0]);
    close(gob1[0]);
    close(gob2[0]);
    close(acka[1]);
    close(ackb[1]);
    char c;
    ssize_t ra = read(ready_fds[0], &c, 1); /* A 已 load */
    ssize_t rb = read(ready_fds[0], &c, 1); /* B 已 load */
    (void)ra;
    (void)rb;
    int st;
    ssize_t ga = write(goa[1], "g", 1); /* 放行 A save */
    (void)ga;
    waitpid(a, &st, 0);
    ssize_t sca = read(acka[0], &c, 1); /* A saved */
    (void)sca;
    ssize_t gb = write(gob1[1], "g", 1); /* 放行 B save */
    (void)gb;
    ssize_t scb = read(ackb[0], &c, 1); /* B saved */
    (void)scb;
    ssize_t gc = write(gob2[1], "g", 1); /* 放行 B clean */
    (void)gc;
    waitpid(b, &st, 0);
    close(ready_fds[0]);
    close(goa[1]);
    close(gob1[1]);
    close(gob2[1]);
    close(acka[0]);
    close(ackb[0]);

    /* 父进程读盘验证：rNET_delay 恰 1 条且 inactive（无 ghost、clean 生效） */
    state_reset();
    state_load();
    struct delay_count_ctx v = {0, 0};
    state_for_each_all(visit_count_delay, &v);
    ASSERT_INT_EQ(v.cnt, 1);        /* 修复前=2: delay@2 active + delay@3 ghost */
    ASSERT_INT_EQ(v.active_cnt, 0); /* 修复前=1: delay@2 未被清 */
    unlink(fp);
    return 0;
}

/* W3 回归：force 重注入同参数序列中，"新注入"不得被 merge 第 2 段(uid+params 忽略
 * active 匹配)写进磁盘同参数但 inactive 的历史记录（复活历史），必须追加新 id。
 * 序列（真实 dcat force 语义，state 层模拟）：
 *   1. inject delay50        → 快照 A：id=1 active (delay50)
 *   2. force inject delay80  → mark_inactive(id1); add → id=2 active (delay80)
 *   3. force inject delay50  → mark_inactive(id2); add → id=3 active (delay50)
 * 修复前：第 3 步 save 时 merge 对 mem.ad(delay50,active) 匹配磁盘 id=1(delay50,inactive)
 *         同 uid+params → 覆盖 id1 active（复活历史）、id3 不落盘 → 盘上 id1 active + id2
 *         inactive，返回的 id3 无处可查，物理 qdisc 仍是 delay50。
 * 修复后：第 2 段仅匹配磁盘 active 目标，delay50 的 inactive 历史不命中 → 追加 id3 active。 */
static int force_round(const char *fp, const char *uid, params_t *p, long long *out_id) {
    state_reset();
    state_set_file(fp);
    state_load();
    /* clean 全部该 uid 活跃记录（force 语义：先清后注） */
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (state_find_by_id((long long)(i + 1)) && strcmp(state_find_by_id((long long)(i + 1))->uid, uid) == 0)
            state_mark_inactive((long long)(i + 1));
    }
    long long id = state_add(uid, p);
    if (id < 1) return 1;
    state_save();
    if (out_id) *out_id = id;
    return 0;
}

int test_reinject_same_params_no_history_revival(void) {
    state_reset();
    const char *fp = "/tmp/dcat-test-state-w3.json";
    state_set_file(fp);
    unlink(fp);

    params_t p50, p80;
    params_init(&p50);
    params_set(&p50, "iface", "eth9");
    params_set(&p50, "delay_ms", "50");
    params_init(&p80);
    params_set(&p80, "iface", "eth9");
    params_set(&p80, "delay_ms", "80");

    long long id1 = 0, id2 = 0, id3 = 0;
    ASSERT_INT_EQ(force_round(fp, "rNET_delay", &p50, &id1), 0);
    ASSERT_INT_EQ(force_round(fp, "rNET_delay", &p80, &id2), 0);
    ASSERT_TRUE(id1 >= 1 && id2 >= 1 && id2 > id1);
    ASSERT_INT_EQ(force_round(fp, "rNET_delay", &p50, &id3), 0);
    ASSERT_TRUE(id3 > id2); /* 新 id 递增 */

    /* 读盘验证：最终应只有延迟 50 的 active(id3)，且无 ghost/复活(id1 保持 inactive) */
    state_reset();
    state_load();
    struct w3_ctx w = {0, 0, 0, 0, -1};
    state_for_each_all(visit_w3, &w);
    /* 历史保留：id1(50, inactive) + id2(80, inactive) + id3(50, active) */
    ASSERT_INT_EQ(w.cnt50, 2);      /* 历史 delay50 + 新 delay50 */
    ASSERT_INT_EQ(w.act50, 1);      /* 只有一条 delay50 active(新注入 id3) */
    ASSERT_INT_EQ(w.cnt80, 1);      /* delay80 历史 */
    ASSERT_INT_EQ(w.act80, 0);      /* delay80 已被 force 清 */
    ASSERT_INT_EQ(w.found_id, id3); /* 盘上 active delay50 必须是 id3，不是复活的 id1 */
    unlink(fp);
    return 0;
}

/* 回归：state.json 已满 32 条(全 inactive)时，新 inject 记录必须落盘——
 * 淘汰最旧 inactive 历史记录腾位，而不是在 merge_records 里被静默丢弃。
 * 复现路径：load 满表 → state_add(内存复用 inactive 槽看似成功) → state_save
 * 第一段把 32 条磁盘记录全拷进 out，第二段因 n==MAX 恒假 → 新记录丢失。 */
int test_save_full_disk_evicts_inactive(void) {
    state_reset();
    const char *fp = "/tmp/dcat-test-state-full.json";
    state_set_file(fp);
    FILE *f = fopen(fp, "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "{\"next_id\":%d,\"records\":[", DCAT_MAX_RECORDS + 1);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        fprintf(f,
                "%s{\"record_id\":%d,\"uid\":\"rNET_delay\",\"params\":{\"iface\":\"old%d\"},"
                "\"started_at\":\"2026-01-01 00:00:%02d\",\"active\":false}",
                i ? "," : "", i + 1, i, i);
    }
    fputs("]}", f);
    fclose(f);

    state_load();
    ASSERT_INT_EQ(state_list_active(), 0);

    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth9");
    long long id = state_add("rCPU_overload", &p);
    ASSERT_TRUE(id > 0);
    state_save();

    state_reset();
    state_load();
    params_t q;
    params_init(&q);
    params_set(&q, "iface", "eth9");
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &q, ids, DCAT_MAX_RECORDS), 1);
    ASSERT_INT_EQ(ids[0], id);
    int cnt = 0;
    state_for_each_all(visit_count, &cnt);
    ASSERT_INT_EQ(cnt, DCAT_MAX_RECORDS); /* 仍受 256 上限约束，未越界 */

    /* next_id 必须随满表注入落盘递增，否则反复注入复用同一 id 会再次丢失。 */
    state_mark_inactive(id);
    state_save();
    state_reset();
    state_load();
    long long id2 = state_add("rCPU_overload", &p);
    ASSERT_TRUE(id2 > id);
    state_save();
    state_reset();
    state_load();
    params_t q2;
    params_init(&q2);
    params_set(&q2, "iface", "eth9");
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &q2, ids, DCAT_MAX_RECORDS), 1);
    ASSERT_INT_EQ(ids[0], id2);
    unlink(fp);
    return 0;
}

/* 满表 31 active + 1 inactive：新注入落盘且只淘汰那条 inactive，active 达满 32。 */
int test_save_full_disk_evicts_inactive_keeps_active(void) {
    state_reset();
    const char *fp = "/tmp/dcat-test-state-full-active.json";
    state_set_file(fp);
    FILE *f = fopen(fp, "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "{\"next_id\":%d,\"records\":[", DCAT_MAX_RECORDS + 1);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        fprintf(f,
                "%s{\"record_id\":%d,\"uid\":\"rNET_delay\",\"params\":{\"iface\":\"a%d\"},"
                "\"started_at\":\"2026-01-01 00:00:%02d\",\"active\":%s}",
                i ? "," : "", i + 1, i, i, i < DCAT_MAX_RECORDS - 1 ? "true" : "false");
    }
    fputs("]}", f);
    fclose(f);

    state_load();
    ASSERT_INT_EQ(state_list_active(), DCAT_MAX_RECORDS - 1);

    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth9");
    long long id = state_add("rCPU_overload", &p);
    ASSERT_TRUE(id > 0);
    state_save();

    state_reset();
    state_load();
    ASSERT_INT_EQ(state_list_active(), DCAT_MAX_RECORDS);
    params_t q;
    params_init(&q);
    params_set(&q, "iface", "eth9");
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &q, ids, DCAT_MAX_RECORDS), 1);
    ASSERT_INT_EQ(ids[0], id);
    unlink(fp);
    return 0;
}

/* 回归：同 uid + 完全同参数的两次真实注入必须各自保留一条记录。
 * 修复前"逻辑匹配"会把磁盘上同参 active 记录当成同一逻辑记录合并 → 只剩 1 条
 * （插件类故障无 reinject 拦截时，注入了 N 次、只记 1 条、clean 也只清一份）。 */
int test_save_keeps_duplicate_same_params(void) {
    state_reset();
    const char *fp = "/tmp/dcat-test-state-dup.json";
    state_set_file(fp);
    FILE *f = fopen(fp, "w");
    ASSERT_TRUE(f != NULL);
    fputs("{\"next_id\":2,\"records\":[{\"record_id\":1,\"uid\":\"rNET_delay\","
          "\"params\":{\"iface\":\"eth0\"},\"started_at\":\"2026-01-01 00:00:01\","
          "\"active\":true}]}",
          f);
    fclose(f);

    state_load();
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    long long id2 = state_add("rNET_delay", &p);
    ASSERT_TRUE(id2 > 1);
    state_save();

    state_reset();
    state_load();
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rNET_delay", &p, ids, DCAT_MAX_RECORDS), 2);
    ASSERT_INT_EQ(state_list_active(), 2);
    unlink(fp);
    return 0;
}

/* 容量契约：活跃/总记录上限为 256；填满后追加必须失败（由 dispatch 预检拦截）。 */
int test_state_capacity_is_256(void) {
    ASSERT_INT_EQ(DCAT_MAX_RECORDS, 256);
    state_reset();
    params_t p;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        params_init(&p);
        char v[16];
        snprintf(v, sizeof v, "eth%d", i);
        params_set(&p, "iface", v);
        ASSERT_TRUE(state_add("rNET_delay", &p) > 0);
    }
    ASSERT_INT_EQ(state_list_active(), DCAT_MAX_RECORDS);
    params_init(&p);
    params_set(&p, "iface", "overflow");
    ASSERT_TRUE(state_add("rNET_delay", &p) == -1);
    return 0;
}

int test_state_can_add(void) {
    state_reset();
    ASSERT_INT_EQ(state_can_add(), 1);
    long long first = 0;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        params_t p;
        params_init(&p);
        char v[16];
        snprintf(v, sizeof v, "eth%d", i);
        params_set(&p, "iface", v);
        long long id = state_add("rNET_delay", &p);
        ASSERT_TRUE(id > 0);
        if (i == 0) first = id;
    }
    ASSERT_INT_EQ(state_can_add(), 0); /* 全 active：无位可加 */
    state_mark_inactive(first);
    ASSERT_INT_EQ(state_can_add(), 1); /* 有 inactive 可淘汰 */
    return 0;
}

/* 回归：磁盘 256 条全 active、内存因刚 clean 出现过 inactive 槽 → state_add 成功，
 * 但落盘时磁盘仍全 active，新记录无处可放。state_save 必须返回 -1（不得静默成功）。 */
int test_save_reports_drop_when_disk_full(void) {
    state_reset();
    const char *fp = "/tmp/dcat-test-state-drop.json";
    state_set_file(fp);
    FILE *f = fopen(fp, "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "{\"next_id\":%d,\"records\":[", DCAT_MAX_RECORDS + 1);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        fprintf(f,
                "%s{\"record_id\":%d,\"uid\":\"rNET_delay\",\"params\":{\"iface\":\"a%d\"},"
                "\"started_at\":\"2026-01-01 00:00:%02d\",\"active\":true}",
                i ? "," : "", i + 1, i, i % 60);
    }
    fputs("]}", f);
    fclose(f);

    state_load();
    ASSERT_INT_EQ(state_list_active(), DCAT_MAX_RECORDS);
    state_mark_inactive(1); /* 内存出现 inactive 槽（磁盘仍全 active） */

    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth9");
    ASSERT_TRUE(state_add("rCPU_overload", &p) > 0);
    ASSERT_INT_EQ(state_save(), -1); /* 新记录无法落盘：必须显式失败 */

    /* 未落盘记录须可取回，供 dispatch 用精确参数回滚。 */
    injection_record_t dr;
    ASSERT_INT_EQ(state_pop_dropped(&dr), 1);
    ASSERT_STREQ(dr.uid, "rCPU_overload");
    ASSERT_STREQ(params_find(&dr.params, "iface"), "eth9");
    ASSERT_INT_EQ(state_pop_dropped(&dr), 0);

    state_reset();
    state_load();
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &p, ids, DCAT_MAX_RECORDS), 0);
    unlink(fp);
    return 0;
}

/* 回归：写盘失败(非表满，如磁盘满/权限)时，本次新增的注入同样要能取回并回滚，
 * 不能只置 dirty 就把"已生效但没记录"的故障留在系统里。 */
int test_save_write_failure_rolls_back_added(void) {
    state_reset();
    const char *dir = "/tmp/dcat-test-state-wfail";
    unlink(dir);
    rmdir(dir);
    unlink("/tmp/dcat-test-state-wfail.lck");
    unlink("/tmp/dcat-test-state-wfail.tmp");
    ASSERT_INT_EQ(mkdir(dir, 0755), 0);
    state_set_file(dir); /* 目标是目录 → 原子 rename 必然失败 */

    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth9");
    ASSERT_TRUE(state_add("rCPU_overload", &p) > 0);
    ASSERT_INT_EQ(state_save(), -1); /* 写盘失败必须显式返回失败 */

    injection_record_t dr;
    ASSERT_INT_EQ(state_pop_dropped(&dr), 1); /* 新增注入须可取回以回滚 */
    ASSERT_STREQ(dr.uid, "rCPU_overload");
    ASSERT_INT_EQ(state_pop_dropped(&dr), 0);

    unlink("/tmp/dcat-test-state-wfail.lck");
    unlink("/tmp/dcat-test-state-wfail.tmp");
    rmdir(dir);
    return 0;
}

/* 回归：state_lock 失败(不可创建路径)属于早期失败，也必须把新增注入转入回滚队列。 */
int test_save_lock_failure_rolls_back_added(void) {
    state_reset();
    state_set_file("/proc/dcat-nonexistent/state.json");
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth9");
    ASSERT_TRUE(state_add("rCPU_overload", &p) > 0);
    ASSERT_INT_EQ(state_save(), -1);
    injection_record_t dr;
    ASSERT_INT_EQ(state_pop_dropped(&dr), 1);
    ASSERT_STREQ(dr.uid, "rCPU_overload");
    ASSERT_INT_EQ(state_pop_dropped(&dr), 0);
    return 0;
}

/* 回归：回滚后须从内存移除，否则后续成功 save 会把已回滚故障当 active 写回(ghost)。 */
int test_remove_after_failed_save_no_ghost(void) {
    state_reset();
    state_set_file("/proc/dcat-nonexistent/state.json");
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth9");
    ASSERT_TRUE(state_add("rCPU_overload", &p) > 0);
    ASSERT_INT_EQ(state_save(), -1);
    injection_record_t dr;
    ASSERT_INT_EQ(state_pop_dropped(&dr), 1);
    state_remove(dr.token, dr.record_id); /* 模拟回滚后清理内存 */

    const char *fp = "/tmp/dcat-test-state-recovered.json";
    unlink(fp);
    state_set_file(fp);
    ASSERT_INT_EQ(state_save(), 0); /* 切到可写路径重试保存 */
    state_reset();
    state_load();
    long long ids[DCAT_MAX_RECORDS];
    ASSERT_INT_EQ(state_find_by_params("rCPU_overload", &p, ids, DCAT_MAX_RECORDS), 0);
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
    RUN_TEST(test_concurrent_same_params_no_loss);
    RUN_TEST(test_clean_after_renumber_no_ghost);
    RUN_TEST(test_reinject_same_params_no_history_revival);
    RUN_TEST(test_save_full_disk_evicts_inactive);
    RUN_TEST(test_save_full_disk_evicts_inactive_keeps_active);
    RUN_TEST(test_save_keeps_duplicate_same_params);
    RUN_TEST(test_state_capacity_is_256);
    RUN_TEST(test_state_can_add);
    RUN_TEST(test_save_reports_drop_when_disk_full);
    RUN_TEST(test_save_write_failure_rolls_back_added);
    RUN_TEST(test_save_lock_failure_rolls_back_added);
    RUN_TEST(test_remove_after_failed_save_no_ghost);
    return TEST_MAIN_RETURN();
}
