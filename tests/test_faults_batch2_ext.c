/* tests/test_faults_batch2_ext.c — Tier 1: batch2 extensions to existing modules
 * cpu(3) + storage(6) + network(2) + process(3) + npu(4) = 18 */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* ---- cpu extensions ---- */
    /* rCPU_quota (optional cg_path) */
    {
        params_t p = mkparams("quota_pct", "50", "cg_path", "/sys/fs/cgroup/dcat_t", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rCPU_quota", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("cpu_quota.sh");
        check_param_env("quota_pct", "50"); check_param_env("cg_path", "/sys/fs/cgroup/dcat_t"); result_free(r);
        r = dispatch_route("rCPU_quota", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rCPU_freq (cores,freq_mhz) */
    {
        params_t p = mkparams("cores", "0", "freq_mhz", "1200", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rCPU_freq", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("cpu_freq.sh");
        check_param_env("cores", "0"); check_param_env("freq_mhz", "1200"); result_free(r);
        r = dispatch_route("rCPU_freq", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rCPU_core_hang */
    {
        params_t p = mkparams("cores", "0-1", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rCPU_core_hang", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("cpu_core_hang.sh");
        check_param_env("cores", "0-1"); result_free(r);
        r = dispatch_route("rCPU_core_hang", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    /* ---- storage extensions ---- */
    /* rDISK_part_full (path, optional size) */
    {
        params_t p = mkparams("path", "/tmp", "size", "100M", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rDISK_part_full", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("disk_part_full.sh");
        check_param_env("path", "/tmp"); check_param_env("size", "100M"); result_free(r);
        r = dispatch_route("rDISK_part_full", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rDISK_inode_exhaust (path, optional count) */
    {
        params_t p = mkparams("path", "/tmp", "count", "1000", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rDISK_inode_exhaust", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("disk_inode_exhaust.sh");
        check_param_env("path", "/tmp"); check_param_env("count", "1000"); result_free(r);
        r = dispatch_route("rDISK_inode_exhaust", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rDISK_io_delay (device,delay_ms) */
    {
        params_t p = mkparams("device", "/dev/loop0", "delay_ms", "50", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rDISK_io_delay", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("disk_io_delay.sh");
        check_param_env("device", "/dev/loop0"); check_param_env("delay_ms", "50"); result_free(r);
        r = dispatch_route("rDISK_io_delay", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rDISK_io_error (device) */
    {
        params_t p = mkparams("device", "/dev/loop0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rDISK_io_error", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("disk_io_error.sh");
        check_param_env("device", "/dev/loop0"); result_free(r);
        r = dispatch_route("rDISK_io_error", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rDISK_scsi_error (device) */
    {
        params_t p = mkparams("device", "/dev/sdb", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rDISK_scsi_error", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("disk_scsi_error.sh");
        check_param_env("device", "/dev/sdb"); result_free(r);
        r = dispatch_route("rDISK_scsi_error", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rDISK_loss (device) */
    {
        params_t p = mkparams("device", "/dev/sdb", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rDISK_loss", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("disk_loss.sh");
        check_param_env("device", "/dev/sdb"); result_free(r);
        r = dispatch_route("rDISK_loss", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    /* ---- network extensions ---- */
    /* rNET_corrupt (iface,corrupt_pct) */
    {
        params_t p = mkparams("iface", "eth0", "corrupt_pct", "10", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_corrupt", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_corrupt.sh");
        check_param_env("iface", "eth0"); check_param_env("corrupt_pct", "10"); result_free(r);
        r = dispatch_route("rNET_corrupt", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNET_conn_exhaust (target, optional count) */
    {
        params_t p = mkparams("target", "127.0.0.1:8080", "count", "500", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNET_conn_exhaust", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("net_conn_exhaust.sh");
        check_param_env("target", "127.0.0.1:8080"); check_param_env("count", "500"); result_free(r);
        r = dispatch_route("rNET_conn_exhaust", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    /* ---- process extensions ---- */
    /* rPROC_fork_bomb (count) */
    {
        params_t p = mkparams("count", "100", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rPROC_fork_bomb", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("proc_fork_bomb.sh");
        check_param_env("count", "100"); result_free(r);
        r = dispatch_route("rPROC_fork_bomb", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rPROC_loop (threads) */
    {
        params_t p = mkparams("threads", "2", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rPROC_loop", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("proc_loop.sh");
        check_param_env("threads", "2"); result_free(r);
        r = dispatch_route("rPROC_loop", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rPROC_fd_exhaust (count) */
    {
        params_t p = mkparams("count", "0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rPROC_fd_exhaust", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("proc_fd_exhaust.sh");
        check_param_env("count", "0"); result_free(r);
        r = dispatch_route("rPROC_fd_exhaust", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    /* ---- npu extensions ---- */
    /* rNPU_freq_down (chip,freq) */
    {
        params_t p = mkparams("chip", "0", "freq", "800", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_freq_down", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("freq_down.sh");
        check_param_env("chip", "0"); check_param_env("freq", "800"); result_free(r);
        r = dispatch_route("rNPU_freq_down", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_aic_fault (chip) */
    {
        params_t p = mkparams("chip", "0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_aic_fault", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("aic_fault.sh");
        check_param_env("chip", "0"); result_free(r);
        r = dispatch_route("rNPU_aic_fault", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_aiv_fault (chip) */
    {
        params_t p = mkparams("chip", "0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_aiv_fault", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("aiv_fault.sh");
        check_param_env("chip", "0"); result_free(r);
        r = dispatch_route("rNPU_aiv_fault", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rNPU_hbm_fault (chip) */
    {
        params_t p = mkparams("chip", "0", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rNPU_hbm_fault", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("hbm_fault.sh");
        check_param_env("chip", "0"); result_free(r);
        r = dispatch_route("rNPU_hbm_fault", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    faults_teardown();
    printf("test_faults_batch2_ext: all 18 faults passed\n");
    return 0;
}
