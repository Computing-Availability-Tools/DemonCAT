/* tests/test_faults_cpu_storage.c — Tier 1: mock table-driven tests for CPU (2) + storage (1) */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* ---- rCPU_overload ---- */
    {
        params_t p = mkparams("cores", "0,1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rCPU_overload", "inject", &p);
        CK(r && r->code == 0);
        MOCK_CALLED;
        CMD_CONTAINS("cpu_overload.sh");
        ENV_EQ("DCAT_OP", "inject");
        ENV_EQ("DCAT_UID", "rCPU_overload");
        check_param_env("cores", "0,1");
        CK(strstr(r->json, "record_id") != NULL);
        result_free(r);

        g_mock_called = 0;
        r = dispatch_route("rCPU_overload", "clean", &p);
        CK(r && r->code == 0);
        MOCK_CALLED;
        ENV_EQ("DCAT_OP", "clean");
        result_free(r);
    }

    /* ---- rCPU_core_offline ---- */
    {
        params_t p = mkparams("cores", "0-3", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rCPU_core_offline", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("cpu_core_offline.sh");
        ENV_EQ("DCAT_UID", "rCPU_core_offline");
        check_param_env("cores", "0-3");
        result_free(r);

        g_mock_called = 0;
        r = dispatch_route("rCPU_core_offline", "clean", &p);
        CK(r && r->code == 0);
        ENV_EQ("DCAT_OP", "clean");
        result_free(r);
    }

    /* ---- rDISK_write_overload ---- */
    {
        params_t p = mkparams("device", "/tmp", "workers", "2", "size_mb", "50",
                              NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rDISK_write_overload", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("disk_write_overload.sh");
        ENV_EQ("DCAT_UID", "rDISK_write_overload");
        check_param_env("device", "/tmp");
        check_param_env("workers", "2");
        check_param_env("size_mb", "50");
        result_free(r);

        g_mock_called = 0;
        r = dispatch_route("rDISK_write_overload", "clean", &p);
        CK(r && r->code == 0);
        ENV_EQ("DCAT_OP", "clean");
        result_free(r);
    }

    faults_teardown();
    printf("test_faults_cpu_storage: all 3 faults passed\n");
    return 0;
}
