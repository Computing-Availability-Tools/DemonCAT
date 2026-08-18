/* tests/ut/test_faults_process.c — Tier 1: mock table-driven tests for process (3) */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* rPROC_exit (inject-only: no clean/query, no record_id) */
    SUBTEST("rPROC_exit inject") {
        params_t p = mkparams("pid", "99999", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        g_mock_called = 0;
        result_t *r = dispatch_route("rPROC_exit", "inject", &p);
        CK(r && r->code == 0);
        MOCK_CALLED;
        CMD_CONTAINS("proc_exit.sh");
        ENV_EQ("DCAT_OP", "inject");
        ENV_EQ("DCAT_UID", "rPROC_exit");
        CK(check_param_env("pid", "99999") == 0);
        CK(strstr(r->json, "record_id") == NULL);
        result_free(r);
    }
    SUBTEST("rPROC_exit clean rejected") {
        params_t p = mkparams("pid", "99999", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_exit", "clean", &p);
        CK(r && r->code == 3);
        result_free(r);
    }
    SUBTEST("rPROC_exit query rejected") {
        params_t p = mkparams("pid", "99999", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_exit", "query", &p);
        CK(r && r->code == 3);
        result_free(r);
    }

    /* rPROC_hang */
    SUBTEST("rPROC_hang inject") {
        params_t p = mkparams("pid", "12345", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_hang", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("proc_hang.sh");
        CK(check_param_env("pid", "12345") == 0);
        result_free(r);
    }
    SUBTEST("rPROC_hang clean") {
        params_t p = mkparams("pid", "12345", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_hang", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }

    /* rPROC_zstate */
    SUBTEST("rPROC_zstate inject") {
        params_t p = mkparams("pid", "54321", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_zstate", "inject", &p);
        CK(r && r->code == 0);
        CMD_CONTAINS("proc_zstate.sh");
        CK(check_param_env("pid", "54321") == 0);
        result_free(r);
    }
    SUBTEST("rPROC_zstate clean") {
        params_t p = mkparams("pid", "54321", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        result_t *r = dispatch_route("rPROC_zstate", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);
    }

    faults_teardown();
    return FAULTS_MAIN_RETURN();
}
