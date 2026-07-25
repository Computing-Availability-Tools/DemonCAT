/* tests/test_faults_network.c — Tier 1: mock table-driven tests for network (11) */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* rNET_delay */
    {
        params_t p = mkparams("iface", "eth0", "delay_ms", "100", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_delay", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_delay.sh");
        ENV_EQ("DCAT_UID", "rNET_delay"); check_param_env("iface", "eth0"); check_param_env("delay_ms", "100");
        result_free(r);
        r = dispatch_route("rNET_delay", "clean", &p); CK(r && r->code == 0); ENV_EQ("DCAT_OP", "clean"); result_free(r);
    }
    /* rNET_loss */
    {
        params_t p = mkparams("iface", "eth0", "loss_pct", "5", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_loss", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_loss.sh");
        check_param_env("iface", "eth0"); check_param_env("loss_pct", "5");
        result_free(r);
        r = dispatch_route("rNET_loss", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_reorder */
    {
        params_t p = mkparams("iface", "eth0", "reorder_pct", "30", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_reorder", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_reorder.sh");
        check_param_env("reorder_pct", "30"); result_free(r);
        r = dispatch_route("rNET_reorder", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_down */
    {
        params_t p = mkparams("iface", "eth0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_down", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_down.sh"); result_free(r);
        r = dispatch_route("rNET_down", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_degrade (optional speed_mbps) */
    {
        params_t p = mkparams("iface", "eth0", "speed_mbps", "10", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_degrade", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_degrade.sh");
        check_param_env("speed_mbps", "10"); result_free(r);
        r = dispatch_route("rNET_degrade", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_port_occupy (optional protocol) */
    {
        params_t p = mkparams("port", "8080", "protocol", "tcp", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_port_occupy", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_port_occupy.sh");
        check_param_env("port", "8080"); check_param_env("protocol", "tcp"); result_free(r);
        r = dispatch_route("rNET_port_occupy", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_service_stop */
    {
        params_t p = mkparams("service", "nginx", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_service_stop", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_service_stop.sh"); result_free(r);
        r = dispatch_route("rNET_service_stop", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_link_flap (optional cycle_sec, count) */
    {
        params_t p = mkparams("iface", "eth0", "cycle_sec", "2", "count", "5",
                              NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_link_flap", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_link_flap.sh");
        check_param_env("cycle_sec", "2"); check_param_env("count", "5"); result_free(r);
        r = dispatch_route("rNET_link_flap", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_bw_limit */
    {
        params_t p = mkparams("iface", "eth0", "rate_kbps", "1000", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_bw_limit", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_bw_limit.sh"); result_free(r);
        r = dispatch_route("rNET_bw_limit", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_jitter */
    {
        params_t p = mkparams("iface", "eth0", "delay_ms", "100", "jitter_ms", "20",
                              NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_jitter", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_jitter.sh");
        check_param_env("jitter_ms", "20"); result_free(r);
        r = dispatch_route("rNET_jitter", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_tcp_loss (optional direction) */
    {
        params_t p = mkparams("port", "443", "direction", "both", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_tcp_loss", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_tcp_loss.sh");
        check_param_env("port", "443"); check_param_env("direction", "both"); result_free(r);
        r = dispatch_route("rNET_tcp_loss", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    faults_teardown();
    printf("test_faults_network: all 11 faults passed\n");
    return 0;
}
