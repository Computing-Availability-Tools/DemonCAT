#include "core/config.h"
#include "core/registry.h"
#include "core/state.h"
#include "core/executor.h"
#include "core/output.h"
#include "core/dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char last_cmd[256];
static char last_op[16];
static int calls;

static result_t *mock(const char *cmd) {
    strncpy(last_cmd, cmd, sizeof last_cmd - 1);
    last_cmd[sizeof last_cmd - 1] = '\0';
    const char *op = getenv("DCAT_OP");
    strncpy(last_op, op ? op : "", sizeof last_op - 1);
    last_op[sizeof last_op - 1] = '\0';
    calls++;
    return result_ok("inject", "x", NULL);
}

#define CK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", #cond); return 1; } } while (0)

static params_t make_params(const char *k1, const char *v1, const char *k2, const char *v2) {
    params_t p; memset(&p, 0, sizeof p);
    p.count = 2;
    strcpy(p.items[0].key, k1); strcpy(p.items[0].value, v1);
    strcpy(p.items[1].key, k2); strcpy(p.items[1].value, v2);
    return p;
}

int main(void) {
    config_t cfg;
    CK(config_load("config/demoncat.conf", &cfg) == 0);
    CK(registry_init(&cfg) == 0);
    state_init("/tmp/dcat_net.json");
    state_set_clean_cb(dispatch_clean_record);
    executor_set_mock(mock);

    /* rNET_loss */
    params_t pl = make_params("iface", "eth0", "loss_pct", "5");
    calls = 0;
    result_t *r = dispatch_inject(registry_find("rNET_loss"), &pl);
    CK(r->code == 0); CK(calls == 1);
    CK(strstr(last_cmd, "net_loss.sh") != NULL);
    CK(strcmp(last_op, "inject") == 0);
    CK(getenv("DCAT_PARAM_IFACE") && !strcmp(getenv("DCAT_PARAM_IFACE"), "eth0"));
    CK(getenv("DCAT_PARAM_LOSS_PCT") && !strcmp(getenv("DCAT_PARAM_LOSS_PCT"), "5"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_loss"), &pl);
    CK(r->code == 0); CK(calls == 1); CK(strcmp(last_op, "clean") == 0);
    result_free(r);

    /* rNET_reorder */
    params_t pr = make_params("iface", "eth0", "reorder_pct", "25");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_reorder"), &pr);
    CK(r->code == 0); CK(strstr(last_cmd, "net_reorder.sh") != NULL);
    CK(getenv("DCAT_PARAM_REORDER_PCT") && !strcmp(getenv("DCAT_PARAM_REORDER_PCT"), "25"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_reorder"), &pr);
    CK(r->code == 0); CK(strcmp(last_op, "clean") == 0);
    result_free(r);

    /* rNET_jitter */
    params_t pj; memset(&pj, 0, sizeof pj);
    pj.count = 3;
    strcpy(pj.items[0].key, "iface"); strcpy(pj.items[0].value, "eth0");
    strcpy(pj.items[1].key, "delay_ms"); strcpy(pj.items[1].value, "100");
    strcpy(pj.items[2].key, "jitter_ms"); strcpy(pj.items[2].value, "20");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_jitter"), &pj);
    CK(r->code == 0); CK(strstr(last_cmd, "net_jitter.sh") != NULL);
    CK(getenv("DCAT_PARAM_JITTER_MS") && !strcmp(getenv("DCAT_PARAM_JITTER_MS"), "20"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_jitter"), &pj);
    CK(r->code == 0);
    result_free(r);

    /* rNET_bw_limit */
    params_t pb = make_params("iface", "eth0", "rate_kbps", "1024");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_bw_limit"), &pb);
    CK(r->code == 0); CK(strstr(last_cmd, "net_bw_limit.sh") != NULL);
    CK(getenv("DCAT_PARAM_RATE_KBPS") && !strcmp(getenv("DCAT_PARAM_RATE_KBPS"), "1024"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_bw_limit"), &pb);
    CK(r->code == 0);
    result_free(r);

    /* rNET_down */
    params_t pd; memset(&pd, 0, sizeof pd);
    pd.count = 1;
    strcpy(pd.items[0].key, "iface"); strcpy(pd.items[0].value, "eth0");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_down"), &pd);
    CK(r->code == 0); CK(strstr(last_cmd, "net_down.sh") != NULL);
    CK(getenv("DCAT_PARAM_IFACE") && !strcmp(getenv("DCAT_PARAM_IFACE"), "eth0"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_down"), &pd);
    CK(r->code == 0); CK(strcmp(last_op, "clean") == 0);
    result_free(r);

    /* rNET_degrade */
    params_t pg; memset(&pg, 0, sizeof pg);
    pg.count = 2;
    strcpy(pg.items[0].key, "iface"); strcpy(pg.items[0].value, "eth0");
    strcpy(pg.items[1].key, "speed_mbps"); strcpy(pg.items[1].value, "10");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_degrade"), &pg);
    CK(r->code == 0); CK(strstr(last_cmd, "net_degrade.sh") != NULL);
    CK(getenv("DCAT_PARAM_SPEED_MBPS") && !strcmp(getenv("DCAT_PARAM_SPEED_MBPS"), "10"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_degrade"), &pg);
    CK(r->code == 0);
    result_free(r);

    /* rNET_port_occupy (background) */
    params_t pp; memset(&pp, 0, sizeof pp);
    pp.count = 2;
    strcpy(pp.items[0].key, "port"); strcpy(pp.items[0].value, "8080");
    strcpy(pp.items[1].key, "protocol"); strcpy(pp.items[1].value, "tcp");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_port_occupy"), &pp);
    CK(r->code == 0); CK(strstr(last_cmd, "net_port_occupy.sh") != NULL);
    CK(getenv("DCAT_PARAM_PORT") && !strcmp(getenv("DCAT_PARAM_PORT"), "8080"));
    CK(getenv("DCAT_PARAM_PROTOCOL") && !strcmp(getenv("DCAT_PARAM_PROTOCOL"), "tcp"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_port_occupy"), &pp);
    CK(r->code == 0); CK(calls == 0);  /* background: clean = executor_kill, no run */
    result_free(r);

    /* rNET_service_stop (dangerous sync) */
    params_t ps; memset(&ps, 0, sizeof ps);
    ps.count = 1;
    strcpy(ps.items[0].key, "service"); strcpy(ps.items[0].value, "nginx");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_service_stop"), &ps);
    CK(r->code == 0); CK(strstr(last_cmd, "net_service_stop.sh") != NULL);
    CK(getenv("DCAT_PARAM_SERVICE") && !strcmp(getenv("DCAT_PARAM_SERVICE"), "nginx"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_service_stop"), &ps);
    CK(r->code == 0); CK(strcmp(last_op, "clean") == 0);
    result_free(r);

    /* rNET_link_flap (background, count) */
    params_t pf; memset(&pf, 0, sizeof pf);
    pf.count = 3;
    strcpy(pf.items[0].key, "iface"); strcpy(pf.items[0].value, "eth0");
    strcpy(pf.items[1].key, "cycle_sec"); strcpy(pf.items[1].value, "2");
    strcpy(pf.items[2].key, "count"); strcpy(pf.items[2].value, "10");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_link_flap"), &pf);
    CK(r->code == 0); CK(strstr(last_cmd, "net_link_flap.sh") != NULL);
    CK(getenv("DCAT_PARAM_COUNT") && !strcmp(getenv("DCAT_PARAM_COUNT"), "10"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_link_flap"), &pf);
    CK(r->code == 0); CK(calls == 0);  /* background: kill */
    result_free(r);

    /* rNET_tcp_loss (iptables sync) */
    params_t pt; memset(&pt, 0, sizeof pt);
    pt.count = 2;
    strcpy(pt.items[0].key, "port"); strcpy(pt.items[0].value, "443");
    strcpy(pt.items[1].key, "direction"); strcpy(pt.items[1].value, "in");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_tcp_loss"), &pt);
    CK(r->code == 0); CK(strstr(last_cmd, "net_tcp_loss.sh") != NULL);
    CK(getenv("DCAT_PARAM_DIRECTION") && !strcmp(getenv("DCAT_PARAM_DIRECTION"), "in"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rNET_tcp_loss"), &pt);
    CK(r->code == 0); CK(strcmp(last_op, "clean") == 0);
    result_free(r);

    unlink("/tmp/dcat_net.json");
    return 0;
}
