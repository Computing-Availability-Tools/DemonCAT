/* tests/test_output.c */
#include "core/output.h"
#include "cJSON.h"
#include <string.h>

int main(void) {
    /* result_ok with NULL data → {"status":"ok","op":"inject","uid":"rX","data":{}} */
    result_t *ok = result_ok("inject", "rCPU_overload", NULL);
    if (!ok || ok->code != DCAT_E_OK || !ok->json) return 1;
    if (!strstr(ok->json, "\"status\":\"ok\"")) return 1;
    if (!strstr(ok->json, "\"op\":\"inject\"")) return 1;
    if (!strstr(ok->json, "\"uid\":\"rCPU_overload\"")) return 1;
    if (!strstr(ok->json, "\"data\":{}")) return 1;
    result_free(ok);

    /* result_ok with data (record_id + message) */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "record_id", 7);
    cJSON_AddStringToObject(data, "message", "done");
    result_t *ok2 = result_ok("inject", "rX", data);
    if (!ok2 || !strstr(ok2->json, "\"record_id\":7")) return 1;
    if (!strstr(ok2->json, "\"message\":\"done\"")) return 1;
    result_free(ok2);

    /* result_ok with NULL uid (list/query without uid) */
    result_t *ok3 = result_ok("list", NULL, NULL);
    if (!ok3 || strstr(ok3->json, "\"uid\"")) return 1;
    result_free(ok3);

    /* result_err */
    result_t *e = result_err("inject", "rX", DCAT_E_PRECHECK, "missing required param: cores");
    if (!e || e->code != DCAT_E_PRECHECK) return 1;
    if (!strstr(e->json, "\"status\":\"error\"")) return 1;
    if (!strstr(e->json, "\"code\":3")) return 1;
    if (!strstr(e->json, "\"message\":\"missing required param: cores\"")) return 1;
    result_free(e);

    /* result_free NULL-safe */
    result_free(NULL);

    return 0;
}
