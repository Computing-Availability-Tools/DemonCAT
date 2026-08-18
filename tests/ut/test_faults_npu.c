/* tests/ut/test_faults_npu.c — Tier 1: mock table-driven tests for NPU (16) */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* rNPU_link_down */
    SUBTEST("rNPU_link_down inject") {
        params_t p = mkparams("chip", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_link_down", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("link_down.sh");
        ENV_EQ("DCAT_UID", "rNPU_link_down");
        CK(check_param_env("chip", "0") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_link_down clean") {
        params_t p = mkparams("chip", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_link_down", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_ip_change */
    SUBTEST("rNPU_ip_change inject") {
        params_t p = mkparams("chip", "0", "address", "192.168.1.100", "netmask", "255.255.255.0",
                              NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_ip_change", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("ip_change.sh");
        CK(check_param_env("address", "192.168.1.100") == 0);
        CK(check_param_env("netmask", "255.255.255.0") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_ip_change clean") {
        params_t p = mkparams("chip", "0", "address", "192.168.1.100", "netmask", "255.255.255.0",
                              NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_ip_change", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_gw_change */
    SUBTEST("rNPU_gw_change inject") {
        params_t p = mkparams("chip", "0", "gateway", "192.168.1.1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_gw_change", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("gw_change.sh");
        CK(check_param_env("gateway", "192.168.1.1") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_gw_change clean") {
        params_t p = mkparams("chip", "0", "gateway", "192.168.1.1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_gw_change", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_netdetect_change */
    SUBTEST("rNPU_netdetect_change inject") {
        params_t p = mkparams("chip", "0", "address", "192.168.1.200", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_netdetect_change", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("netdetect_change.sh");
        result_free(r);
    }
    SUBTEST("rNPU_netdetect_change clean") {
        params_t p = mkparams("chip", "0", "address", "192.168.1.200", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_netdetect_change", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_arp (add) */
    SUBTEST("rNPU_arp inject (add)") {
        params_t p = mkparams("chip", "0", "action", "add", "dev", "eth0", "ip", "192.168.1.50",
                              "mac", "aa:bb:cc:dd:ee:ff", NULL, NULL);
        result_t *r = dispatch_route("rNPU_arp", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("arp.sh");
        CK(check_param_env("mac", "aa:bb:cc:dd:ee:ff") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_arp clean (add)") {
        params_t p = mkparams("chip", "0", "action", "add", "dev", "eth0", "ip", "192.168.1.50",
                              "mac", "aa:bb:cc:dd:ee:ff", NULL, NULL);
        result_t *r = dispatch_route("rNPU_arp", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_arp (del) */
    SUBTEST("rNPU_arp inject (del)") {
        params_t p = mkparams("chip", "0", "action", "del", "dev", "eth0", "ip", "192.168.1.50",
                              NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_arp", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("arp.sh");
        result_free(r);
    }
    SUBTEST("rNPU_arp clean (del)") {
        params_t p = mkparams("chip", "0", "action", "del", "dev", "eth0", "ip", "192.168.1.50",
                              NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_arp", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_route (add) */
    SUBTEST("rNPU_route inject (add)") {
        params_t p = mkparams("chip", "0", "action", "add", "address", "10.0.0.0", "netmask", "255.0.0.0",
                              "gateway", "192.168.1.1", NULL, NULL);
        result_t *r = dispatch_route("rNPU_route", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("route.sh");
        result_free(r);
    }
    SUBTEST("rNPU_route clean (add)") {
        params_t p = mkparams("chip", "0", "action", "add", "address", "10.0.0.0", "netmask", "255.0.0.0",
                              "gateway", "192.168.1.1", NULL, NULL);
        result_t *r = dispatch_route("rNPU_route", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_route (del) */
    SUBTEST("rNPU_route inject (del)") {
        params_t p = mkparams("chip", "0", "action", "del", "address", "10.0.0.0", "netmask", "255.0.0.0",
                              NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_route", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("route.sh");
        result_free(r);
    }
    SUBTEST("rNPU_route clean (del)") {
        params_t p = mkparams("chip", "0", "action", "del", "address", "10.0.0.0", "netmask", "255.0.0.0",
                              NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_route", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_iprule (add) */
    SUBTEST("rNPU_iprule inject (add)") {
        params_t p = mkparams("chip", "0", "action", "add", "dir", "in", "ip", "192.168.1.50",
                              "table", "100", NULL, NULL);
        result_t *r = dispatch_route("rNPU_iprule", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("iprule.sh");
        CK(check_param_env("dir", "in") == 0);
        CK(check_param_env("table", "100") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_iprule clean (add)") {
        params_t p = mkparams("chip", "0", "action", "add", "dir", "in", "ip", "192.168.1.50",
                              "table", "100", NULL, NULL);
        result_t *r = dispatch_route("rNPU_iprule", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_iprule (del) */
    SUBTEST("rNPU_iprule inject (del)") {
        params_t p = mkparams("chip", "0", "action", "del", "dir", "in", "ip", "192.168.1.50",
                              NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_iprule", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("iprule.sh");
        result_free(r);
    }
    SUBTEST("rNPU_iprule clean (del)") {
        params_t p = mkparams("chip", "0", "action", "del", "dir", "in", "ip", "192.168.1.50",
                              NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_iprule", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_iproute (add) */
    SUBTEST("rNPU_iproute inject (add)") {
        params_t p = mkparams("chip", "0", "action", "add", "ip", "10.0.0.1", "ip_mask", "255.0.0.0",
                              "table", "100", NULL, NULL);
        params_set(&p, "via", "192.168.1.1");
        params_set(&p, "dev", "eth0");
        result_t *r = dispatch_route("rNPU_iproute", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("iproute.sh");
        CK(check_param_env("ip_mask", "255.0.0.0") == 0);
        CK(check_param_env("via", "192.168.1.1") == 0);
        CK(check_param_env("dev", "eth0") == 0);
        CK(check_param_env("table", "100") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_iproute clean (add)") {
        params_t p = mkparams("chip", "0", "action", "add", "ip", "10.0.0.1", "ip_mask", "255.0.0.0",
                              "table", "100", NULL, NULL);
        params_set(&p, "via", "192.168.1.1");
        params_set(&p, "dev", "eth0");
        result_t *r = dispatch_route("rNPU_iproute", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_iproute (del) */
    SUBTEST("rNPU_iproute inject (del)") {
        params_t p = mkparams("chip", "0", "action", "del", "ip", "10.0.0.1", "ip_mask", "255.0.0.0",
                              "table", "100", NULL, NULL);
        result_t *r = dispatch_route("rNPU_iproute", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("iproute.sh");
        result_free(r);
    }
    SUBTEST("rNPU_iproute clean (del)") {
        params_t p = mkparams("chip", "0", "action", "del", "ip", "10.0.0.1", "ip_mask", "255.0.0.0",
                              "table", "100", NULL, NULL);
        result_t *r = dispatch_route("rNPU_iproute", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_bw_limit */
    SUBTEST("rNPU_bw_limit inject") {
        params_t p = mkparams("chip", "0", "bw_limit", "10000", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_bw_limit", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("bw_limit.sh");
        CK(check_param_env("bw_limit", "10000") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_bw_limit clean") {
        params_t p = mkparams("chip", "0", "bw_limit", "10000", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_bw_limit", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_mtu_mismatch */
    SUBTEST("rNPU_mtu_mismatch inject") {
        params_t p = mkparams("chip", "0", "size", "1280", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_mtu_mismatch", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("mtu_mismatch.sh");
        CK(check_param_env("size", "1280") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_mtu_mismatch clean") {
        params_t p = mkparams("chip", "0", "size", "1280", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_mtu_mismatch", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_dscp_tc_change */
    SUBTEST("rNPU_dscp_tc_change inject") {
        params_t p = mkparams("chip", "0", "dscp", "46", "tc", "5", NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_dscp_tc_change", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("dscp_tc_change.sh");
        CK(check_param_env("dscp", "46") == 0);
        CK(check_param_env("tc", "5") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_dscp_tc_change clean") {
        params_t p = mkparams("chip", "0", "dscp", "46", "tc", "5", NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_dscp_tc_change", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_roce_port_change */
    SUBTEST("rNPU_roce_port_change inject") {
        params_t p = mkparams("chip", "0", "port", "4791", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_roce_port_change", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("roce_port_change.sh");
        CK(check_param_env("port", "4791") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_roce_port_change clean") {
        params_t p = mkparams("chip", "0", "port", "4791", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_roce_port_change", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }

    faults_teardown();
    return FAULTS_MAIN_RETURN();
}
