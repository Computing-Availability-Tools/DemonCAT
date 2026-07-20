#include "core/output.h"
#include <string.h>

int main(void) {
    result_t *ok = result_ok("inject", "rCPU_overload", NULL);
    if (!ok || ok->code != DCAT_E_OK || !ok->json) return 1;
    if (!strstr(ok->json, "\"status\":\"ok\"")) return 1;
    if (!strstr(ok->json, "\"op\":\"inject\"")) return 1;
    if (!strstr(ok->json, "\"uid\":\"rCPU_overload\"")) return 1;
    if (!strstr(ok->json, "\"data\":{}")) return 1;
    result_free(ok);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "record_id", 7);
    result_t *ok2 = result_ok("inject", "rX", data);
    if (!ok2 || !strstr(ok2->json, "\"record_id\":7")) return 1;
    result_free(ok2);

    result_t *e = result_err("inject", "rX", DCAT_E_SAFETY, "already active");
    if (!e || e->code != DCAT_E_SAFETY) return 1;
    if (!strstr(e->json, "\"status\":\"error\"")) return 1;
    if (!strstr(e->json, "\"code\":3")) return 1;
    if (!strstr(e->json, "\"message\":\"already active\"")) return 1;
    result_free(e);

    return 0;
}
