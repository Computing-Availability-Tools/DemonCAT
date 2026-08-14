/* tests/test_faults_batch2_newmods.c — Tier 1: batch2 new modules
 * memory(4) + filesystem(4) + docker(2) + system(2 inject-only) = 12 */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* ---- memory ---- */
    /* rMEM_leak */
    {
        params_t p = mkparams("size_mb", "512", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rMEM_leak", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("mem_leak.sh");
        ENV_EQ("DCAT_UID", "rMEM_leak"); check_param_env("size_mb", "512");
        result_free(r);
        r = dispatch_route("rMEM_leak", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rMEM_oom */
    {
        params_t p = mkparams("rate_mb", "64", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rMEM_oom", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("mem_oom.sh");
        check_param_env("rate_mb", "64"); result_free(r);
        r = dispatch_route("rMEM_oom", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rMEM_fragment (optional block_kb) */
    {
        params_t p = mkparams("blocks", "200", "block_kb", "1024", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rMEM_fragment", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("mem_fragment.sh");
        check_param_env("blocks", "200"); check_param_env("block_kb", "1024"); result_free(r);
        r = dispatch_route("rMEM_fragment", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rMEM_swap_overload */
    {
        params_t p = mkparams("size_mb", "8192", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rMEM_swap_overload", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("mem_swap_overload.sh");
        check_param_env("size_mb", "8192"); result_free(r);
        r = dispatch_route("rMEM_swap_overload", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    /* ---- filesystem ---- */
    /* rFS_file_lock (path,mode) */
    {
        params_t p = mkparams("path", "/tmp/dcat_t", "mode", "nodelete", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rFS_file_lock", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("fs_file_lock.sh");
        check_param_env("path", "/tmp/dcat_t"); check_param_env("mode", "nodelete"); result_free(r);
        r = dispatch_route("rFS_file_lock", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rFS_iowait_high (path + workers) */
    {
        params_t p = mkparams("path", "/tmp", "workers", "4", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rFS_iowait_high", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("fs_iowait_high.sh");
        check_param_env("path", "/tmp"); check_param_env("workers", "4"); result_free(r);
        r = dispatch_route("rFS_iowait_high", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    /* ---- docker ---- */
    /* rDOCKER_kill */
    {
        params_t p = mkparams("container", "web", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rDOCKER_kill", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("docker_kill.sh");
        check_param_env("container", "web"); result_free(r);
        r = dispatch_route("rDOCKER_kill", "clean", &p); CK(r && r->code == 0); result_free(r);
    }
    /* rDOCKER_mem_overload (container,size) */
    {
        params_t p = mkparams("container", "web", "size", "512M", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_route("rDOCKER_mem_overload", "inject", &p);
        CK(r && r->code == 0); CMD_CONTAINS("docker_mem_overload.sh");
        check_param_env("container", "web"); check_param_env("size", "512M"); result_free(r);
        r = dispatch_route("rDOCKER_mem_overload", "clean", &p); CK(r && r->code == 0); result_free(r);
    }

    /* ---- system (inject-only) ---- */
    /* rSYS_panic */
    {
        params_t p = mkparams(NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rSYS_panic", "inject", &p);
        CK(r && r->code == 0); MOCK_CALLED; CMD_CONTAINS("sys_panic.sh");
        ENV_EQ("DCAT_UID", "rSYS_panic");
        CK(strstr(r->json, "record_id") == NULL);   /* inject-only: no record_id */
        result_free(r);
        r = dispatch_route("rSYS_panic", "clean", &p); CK(r && r->code == 3); result_free(r);
        r = dispatch_route("rSYS_panic", "query", &p); CK(r && r->code == 3); result_free(r);
    }
    /* rSYS_poweroff (inject-only, mode required) */
    {
        params_t p = mkparams("mode", "1", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rSYS_poweroff", "inject", &p);
        CK(r && r->code == 0); MOCK_CALLED; CMD_CONTAINS("sys_poweroff.sh");
        ENV_EQ("DCAT_UID", "rSYS_poweroff");
        check_param_env("mode", "1");
        CK(strstr(r->json, "record_id") == NULL);   /* inject-only: no record_id */
        result_free(r);
        r = dispatch_route("rSYS_poweroff", "clean", &p); CK(r && r->code == 3); result_free(r);
        r = dispatch_route("rSYS_poweroff", "query", &p); CK(r && r->code == 3); result_free(r);
    }

    faults_teardown();
    printf("test_faults_batch2_newmods: all 10 faults passed\n");
    return 0;
}
