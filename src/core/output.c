/* src/core/output.c */
#include "output.h"

#include <stdio.h>
#include <stdlib.h>

result_t *result_ok(const char *op, const char *uid, cJSON *data) {
    result_t *r = malloc(sizeof *r);
    if (!r) return NULL;
    if (!data) data = cJSON_CreateObject();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    if (op) cJSON_AddStringToObject(root, "op", op);
    if (uid && uid[0]) cJSON_AddStringToObject(root, "uid", uid);
    cJSON_AddItemToObject(root, "data", data);
    r->code = DCAT_E_OK;
    r->json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return r;
}

result_t *result_err(const char *op, const char *uid, int code, const char *msg) {
    result_t *r = malloc(sizeof *r);
    if (!r) return NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    if (op) cJSON_AddStringToObject(root, "op", op);
    if (uid && uid[0]) cJSON_AddStringToObject(root, "uid", uid);
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    if (msg) cJSON_AddStringToObject(err, "message", msg);
    cJSON_AddItemToObject(root, "error", err);
    r->code = code;
    r->json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return r;
}

void output_print(const result_t *r) {
    if (!r) {
        printf("{\"status\":\"error\",\"error\":{\"code\":%d,\"message\":\"null result\"}}\n",
               DCAT_E_RUN);
        return;
    }
    printf("%s\n", r->json ? r->json : "{}");
}

void result_free(result_t *r) {
    if (!r) return;
    free(r->json);
    free(r);
}
