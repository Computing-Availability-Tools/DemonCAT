/* tests/test_faults_batch2_ext.c — Tier 1: batch2 extensions to existing modules
 * cpu(3) + storage(5) + network(2) + process(3) + npu(4) = 16 */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* ---- cpu extensions ---- */
    /* rCPU_quota (cores + quota_pct) */
    SUBTEST("rCPU_quota inject") {
        params_t p = mkparams("cores", "0", "quota_pct", "50", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rCPU_quota", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("cpu_quota.sh");
        CK(check_param_env("cores", "0") == 0);
        CK(check_param_env("quota_pct", "50") == 0);
        result_free(r);
    }
    SUBTEST("rCPU_quota clean") {
        params_t p = mkparams("cores", "0", "quota_pct", "50", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rCPU_quota", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rCPU_freq (cores,freq_mhz) */
    SUBTEST("rCPU_freq inject") {
        params_t p = mkparams("cores", "0", "freq_mhz", "1200", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rCPU_freq", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("cpu_freq.sh");
        CK(check_param_env("cores", "0") == 0);
        CK(check_param_env("freq_mhz", "1200") == 0);
        result_free(r);
    }
    SUBTEST("rCPU_freq clean") {
        params_t p = mkparams("cores", "0", "freq_mhz", "1200", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rCPU_freq", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rCPU_core_hang */
    SUBTEST("rCPU_core_hang inject") {
        params_t p = mkparams("cores", "0-1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rCPU_core_hang", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("cpu_core_hang.sh");
        CK(check_param_env("cores", "0-1") == 0);
        result_free(r);
    }
    SUBTEST("rCPU_core_hang clean") {
        params_t p = mkparams("cores", "0-1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rCPU_core_hang", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }

    /* ---- storage extensions ---- */
    /* rDISK_part_full (path, optional size) */
    SUBTEST("rDISK_part_full inject") {
        params_t p = mkparams("path", "/tmp", "size", "100M", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rDISK_part_full", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("disk_part_full.sh");
        CK(check_param_env("path", "/tmp") == 0);
        CK(check_param_env("size", "100M") == 0);
        result_free(r);
    }
    SUBTEST("rDISK_part_full clean") {
        params_t p = mkparams("path", "/tmp", "size", "100M", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rDISK_part_full", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rDISK_inode_exhaust (path, optional count) */
    SUBTEST("rDISK_inode_exhaust inject") {
        params_t p = mkparams("path", "/tmp", "count", "1000", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rDISK_inode_exhaust", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("disk_inode_exhaust.sh");
        CK(check_param_env("path", "/tmp") == 0);
        CK(check_param_env("count", "1000") == 0);
        result_free(r);
    }
    SUBTEST("rDISK_inode_exhaust clean") {
        params_t p = mkparams("path", "/tmp", "count", "1000", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rDISK_inode_exhaust", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rDISK_io_delay (device,delay_ms) */
    SUBTEST("rDISK_io_delay inject") {
        params_t p = mkparams("device", "/dev/loop0", "delay_ms", "50", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rDISK_io_delay", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("disk_io_delay.sh");
        CK(check_param_env("device", "/dev/loop0") == 0);
        CK(check_param_env("delay_ms", "50") == 0);
        result_free(r);
    }
    SUBTEST("rDISK_io_delay clean") {
        params_t p = mkparams("device", "/dev/loop0", "delay_ms", "50", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rDISK_io_delay", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rDISK_io_error (device) */
    SUBTEST("rDISK_io_error inject") {
        params_t p = mkparams("device", "/dev/loop0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rDISK_io_error", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("disk_io_error.sh");
        CK(check_param_env("device", "/dev/loop0") == 0);
        result_free(r);
    }
    SUBTEST("rDISK_io_error clean") {
        params_t p = mkparams("device", "/dev/loop0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rDISK_io_error", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }

    /* ---- network extensions ---- */
    /* rNET_corrupt (iface,corrupt_pct) */
    SUBTEST("rNET_corrupt inject") {
        params_t p = mkparams("iface", "eth0", "corrupt_pct", "10", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNET_corrupt", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("net_corrupt.sh");
        CK(check_param_env("iface", "eth0") == 0);
        CK(check_param_env("corrupt_pct", "10") == 0);
        result_free(r);
    }
    SUBTEST("rNET_corrupt clean") {
        params_t p = mkparams("iface", "eth0", "corrupt_pct", "10", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNET_corrupt", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNET_conn_exhaust (target, optional count) */
    SUBTEST("rNET_conn_exhaust inject") {
        params_t p = mkparams("target", "127.0.0.1:8080", "count", "500", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNET_conn_exhaust", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("net_conn_exhaust.sh");
        CK(check_param_env("target", "127.0.0.1:8080") == 0);
        CK(check_param_env("count", "500") == 0);
        result_free(r);
    }
    SUBTEST("rNET_conn_exhaust clean") {
        params_t p = mkparams("target", "127.0.0.1:8080", "count", "500", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNET_conn_exhaust", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }

    /* ---- process extensions ---- */
    /* rPROC_fork_bomb (count) */
    SUBTEST("rPROC_fork_bomb inject") {
        params_t p = mkparams("count", "100", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_fork_bomb", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("proc_fork_bomb.sh");
        CK(check_param_env("count", "100") == 0);
        result_free(r);
    }
    SUBTEST("rPROC_fork_bomb clean") {
        params_t p = mkparams("count", "100", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_fork_bomb", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rPROC_fd_exhaust (count) */
    SUBTEST("rPROC_fd_exhaust inject") {
        params_t p = mkparams("count", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_fd_exhaust", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("proc_fd_exhaust.sh");
        CK(check_param_env("count", "0") == 0);
        result_free(r);
    }
    SUBTEST("rPROC_fd_exhaust clean") {
        params_t p = mkparams("count", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_fd_exhaust", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }

    /* ---- npu extensions ---- */
    /* rNPU_pcie_down (npu_id,gen) */
    SUBTEST("rNPU_pcie_down inject") {
        params_t p = mkparams("npu_id", "0", "gen", "1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_pcie_down", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("pcie_down.sh");
        CK(check_param_env("npu_id", "0") == 0);
        CK(check_param_env("gen", "1") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_pcie_down clean") {
        params_t p = mkparams("npu_id", "0", "gen", "1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_pcie_down", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_aic_load (chip) */
    SUBTEST("rNPU_aic_load inject") {
        params_t p = mkparams("chip", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_aic_load", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("aic_load.sh");
        CK(check_param_env("chip", "0") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_aic_load clean") {
        params_t p = mkparams("chip", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_aic_load", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_aiv_load (chip) */
    SUBTEST("rNPU_aiv_load inject") {
        params_t p = mkparams("chip", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_aiv_load", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("aiv_load.sh");
        CK(check_param_env("chip", "0") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_aiv_load clean") {
        params_t p = mkparams("chip", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_aiv_load", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_aicpu_load (chip) */
    SUBTEST("rNPU_aicpu_load inject") {
        params_t p = mkparams("chip", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_aicpu_load", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("aicpu_load.sh");
        CK(check_param_env("chip", "0") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_aicpu_load clean") {
        params_t p = mkparams("chip", "0", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_aicpu_load", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }
    /* rNPU_hbm_load (chip, size) */
    SUBTEST("rNPU_hbm_load inject") {
        params_t p = mkparams("chip", "0", "size", "2G", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_hbm_load", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("hbm_load.sh");
        CK(check_param_env("chip", "0") == 0);
        CK(check_param_env("size", "2G") == 0);
        result_free(r);
    }
    SUBTEST("rNPU_hbm_load clean") {
        params_t p = mkparams("chip", "0", "size", "2G", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rNPU_hbm_load", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }

    faults_teardown();
    printf("test_faults_batch2_ext: all 15 faults passed\n");
    return FAULTS_MAIN_RETURN();
}
