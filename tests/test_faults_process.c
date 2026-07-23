/* tests/test_faults_process.c — Tier 1: mock table-driven tests for process (4) */
#include "test_faults_common.h"

int main(void) {
    faults_setup();

    /* rPROC_exit (inject-only: no clean/query, no record_id) */
    {
        params_t p = mkparams("pid", "99999", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        g_mock_called = 0;
        result_t *r = dispatch_inject("rPROC_exit", &p);
        CK(r && r->code == 0);
        MOCK_CALLED;
        CMD_CONTAINS("proc_exit.sh");
        ENV_EQ("DCAT_OP", "inject");
        ENV_EQ("DCAT_UID", "rPROC_exit");
        check_param_env("pid", "99999");
        CK(strstr(r->json, "record_id") == NULL);  /* inject-only: no record_id */
        result_free(r);

        /* clean should be rejected (op not in supported_ops) */
        r = dispatch_clean("rPROC_exit", &p);
        CK(r && r->code == DCAT_E_PRECHECK);
        result_free(r);

        /* query should be rejected */
        r = dispatch_query("rPROC_exit", &p);
        CK(r && r->code == DCAT_E_PRECHECK);
        result_free(r);
    }

    /* rPROC_dstate */
    {
        params_t p = mkparams("count", "2", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_inject("rPROC_dstate", &p);
        CK(r && r->code == 0); CMD_CONTAINS("proc_dstate.sh");
        check_param_env("count", "2"); CK(strstr(r->json, "record_id") != NULL);
        result_free(r);
        r = dispatch_clean("rPROC_dstate", &p); CK(r && r->code == 0); result_free(r);
    }

    /* rPROC_hang */
    {
        params_t p = mkparams("pid", "12345", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_inject("rPROC_hang", &p);
        CK(r && r->code == 0); CMD_CONTAINS("proc_hang.sh");
        check_param_env("pid", "12345"); result_free(r);
        r = dispatch_clean("rPROC_hang", &p); CK(r && r->code == 0); result_free(r);
    }

    /* rPROC_zstate */
    {
        params_t p = mkparams("count", "3", NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL, NULL,NULL);
        result_t *r = dispatch_inject("rPROC_zstate", &p);
        CK(r && r->code == 0); CMD_CONTAINS("proc_zstate.sh");
        check_param_env("count", "3"); result_free(r);
        r = dispatch_clean("rPROC_zstate", &p); CK(r && r->code == 0); result_free(r);
    }

    faults_teardown();
    printf("test_faults_process: all 4 faults passed\n");
    return 0;
}
