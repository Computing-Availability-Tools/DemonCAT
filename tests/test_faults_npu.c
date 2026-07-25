/* tests/test_faults_npu.c — Tier 1: mock table-driven tests for NPU (20) */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* rNPU_link_down */
    {
        params_t p = mkparams("chip", "0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_link_down", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("link_down.sh");
        ENV_EQ("DCAT_UID", "rNPU_link_down"); check_param_env("chip", "0");
        result_free(r);
        r = dispatch_route("rNPU_link_down", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_ip_change */
    {
        params_t p = mkparams("chip", "0", "address", "192.168.1.100", "netmask", "255.255.255.0",
                              NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_ip_change", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("ip_change.sh");
        check_param_env("address", "192.168.1.100"); check_param_env("netmask", "255.255.255.0");
        result_free(r);
        r = dispatch_route("rNPU_ip_change", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_gw_change */
    {
        params_t p = mkparams("chip", "0", "gateway", "192.168.1.1", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_gw_change", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("gw_change.sh");
        check_param_env("gateway", "192.168.1.1"); result_free(r);
        r = dispatch_route("rNPU_gw_change", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_netdetect_change */
    {
        params_t p = mkparams("chip", "0", "address", "192.168.1.200", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_netdetect_change", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("netdetect_change.sh"); result_free(r);
        r = dispatch_route("rNPU_netdetect_change", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_arp_poison */
    {
        params_t p = mkparams("chip", "0", "dev", "eth0", "ip", "192.168.1.50", "mac", "aa:bb:cc:dd:ee:ff",
                              NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_arp_poison", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("arp_poison.sh");
        check_param_env("mac", "aa:bb:cc:dd:ee:ff"); result_free(r);
        r = dispatch_route("rNPU_arp_poison", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_arp_del */
    {
        params_t p = mkparams("chip", "0", "dev", "eth0", "ip", "192.168.1.50", NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_arp_del", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("arp_del.sh"); result_free(r);
        r = dispatch_route("rNPU_arp_del", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_route_add */
    {
        params_t p = mkparams("chip", "0", "address", "10.0.0.0", "netmask", "255.0.0.0", "gateway", "192.168.1.1",
                              NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_route_add", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("route_add.sh"); result_free(r);
        r = dispatch_route("rNPU_route_add", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_route_del */
    {
        params_t p = mkparams("chip", "0", "address", "10.0.0.0", "netmask", "255.0.0.0", NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_route_del", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("route_del.sh"); result_free(r);
        r = dispatch_route("rNPU_route_del", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_route_clear */
    {
        params_t p = mkparams("chip", "0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_route_clear", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("route_clear.sh"); result_free(r);
        r = dispatch_route("rNPU_route_clear", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_iprule_add */
    {
        params_t p = mkparams("chip", "0", "dir", "in", "ip", "192.168.1.50", "table", "100",
                              NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_iprule_add", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("iprule_add.sh");
        check_param_env("dir", "in"); check_param_env("table", "100"); result_free(r);
        r = dispatch_route("rNPU_iprule_add", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_iprule_del */
    {
        params_t p = mkparams("chip", "0", "dir", "in", "ip", "192.168.1.50", NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_iprule_del", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("iprule_del.sh"); result_free(r);
        r = dispatch_route("rNPU_iprule_del", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_iproute_add (6 params) */
    {
        params_t p = mkparams("chip", "0", "ip", "10.0.0.1", "ip_mask", "255.0.0.0",
                              "via", "192.168.1.1", "dev", "eth0", "table", "100");
        result_t *r = dispatch_route("rNPU_iproute_add", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("iproute_add.sh");
        check_param_env("ip_mask", "255.0.0.0"); check_param_env("via", "192.168.1.1");
        check_param_env("dev", "eth0"); check_param_env("table", "100"); result_free(r);
        r = dispatch_route("rNPU_iproute_add", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_iproute_del */
    {
        params_t p = mkparams("chip", "0", "ip", "10.0.0.1", "ip_mask", "255.0.0.0", "table", "100",
                              NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_iproute_del", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("iproute_del.sh"); result_free(r);
        r = dispatch_route("rNPU_iproute_del", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_bw_limit */
    {
        params_t p = mkparams("chip", "0", "bw_limit", "10000", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_bw_limit", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("bw_limit.sh");
        check_param_env("bw_limit", "10000"); result_free(r);
        r = dispatch_route("rNPU_bw_limit", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_mtu_mismatch */
    {
        params_t p = mkparams("chip", "0", "size", "1280", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_mtu_mismatch", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("mtu_mismatch.sh");
        check_param_env("size", "1280"); result_free(r);
        r = dispatch_route("rNPU_mtu_mismatch", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_fec_change */
    {
        params_t p = mkparams("chip", "0", "encoding", "rs", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_fec_change", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("fec_change.sh");
        check_param_env("encoding", "rs"); result_free(r);
        r = dispatch_route("rNPU_fec_change", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_dscp_tc_change */
    {
        params_t p = mkparams("chip", "0", "dscp", "46", "tc", "5", NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_dscp_tc_change", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("dscp_tc_change.sh");
        check_param_env("dscp", "46"); check_param_env("tc", "5"); result_free(r);
        r = dispatch_route("rNPU_dscp_tc_change", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_prio_tc_change */
    {
        params_t p = mkparams("chip", "0", "map", "0,1,2,3,4,5,6,7", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_prio_tc_change", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("prio_tc_change.sh");
        check_param_env("map", "0,1,2,3,4,5,6,7"); result_free(r);
        r = dispatch_route("rNPU_prio_tc_change", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_pfc_change */
    {
        params_t p = mkparams("chip", "0", "bitmap", "1,0,0,0,0,0,0,0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_pfc_change", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("pfc_change.sh");
        check_param_env("bitmap", "1,0,0,0,0,0,0,0"); result_free(r);
        r = dispatch_route("rNPU_pfc_change", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_roce_port_change */
    {
        params_t p = mkparams("chip", "0", "port", "4791", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_roce_port_change", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("roce_port_change.sh");
        check_param_env("port", "4791"); result_free(r);
        r = dispatch_route("rNPU_roce_port_change", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    faults_teardown();
    printf("test_faults_npu: all 20 faults passed\n");
    return 0;
}
