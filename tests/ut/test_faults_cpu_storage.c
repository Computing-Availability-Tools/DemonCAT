/* tests/ut/test_faults_cpu_storage.c — Tier 1: mock table-driven tests for CPU (2) + storage (1) */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* ---- rCPU_overload ---- */
    SUBTEST("rCPU_overload inject") {
        params_t p = mkparams("cores", "0,1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rCPU_overload", "inject", &p);
        CK(r && r->code == 0);
        MOCK_CALLED;
        CMD_CONTAINS("cpu_overload.sh");
        ENV_EQ("DCAT_OP", "inject");
        ENV_EQ("DCAT_UID", "rCPU_overload");
        CK(check_param_env("cores", "0,1") == 0);
        CK(strstr(r->json, "record_id") != NULL);
        result_free(r);
    }
    SUBTEST("rCPU_overload clean") {
        params_t p = mkparams("cores", "0,1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rCPU_overload", "clean", &p);
        CK(r && r->code == 0);
        MOCK_CALLED;
        ENV_EQ("DCAT_OP", "clean");
        result_free(r);
    }

    /* ---- rCPU_core_offline ---- */
    SUBTEST("rCPU_core_offline inject") {
        params_t p = mkparams("cores", "0-3", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rCPU_core_offline", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("cpu_core_offline.sh");
        ENV_EQ("DCAT_UID", "rCPU_core_offline");
        CK(check_param_env("cores", "0-3") == 0);
        result_free(r);
    }
    SUBTEST("rCPU_core_offline clean") {
        params_t p = mkparams("cores", "0-3", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rCPU_core_offline", "clean", &p);
        CK(r && r->code == 0);
        ENV_EQ("DCAT_OP", "clean");
        result_free(r);
    }

    /* ---- rDISK_write_overload ---- */
    SUBTEST("rDISK_write_overload inject") {
        params_t p = mkparams("device", "/tmp", "workers", "2", "size_mb", "50",
                              NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rDISK_write_overload", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("disk_write_overload.sh");
        ENV_EQ("DCAT_UID", "rDISK_write_overload");
        CK(check_param_env("device", "/tmp") == 0);
        CK(check_param_env("workers", "2") == 0);
        CK(check_param_env("size_mb", "50") == 0);
        result_free(r);
    }
    SUBTEST("rDISK_write_overload clean") {
        params_t p = mkparams("device", "/tmp", "workers", "2", "size_mb", "50",
                              NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rDISK_write_overload", "clean", &p);
        CK(r && r->code == 0);
        ENV_EQ("DCAT_OP", "clean");
        result_free(r);
    }

    faults_teardown();
    return FAULTS_MAIN_RETURN();
}
