#include "output.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

result_t *result_ok(const char *op, const char *uid, long long record_id, const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", op);
    if (uid) cJSON_AddStringToObject(root, "uid", uid);
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (message) cJSON_AddStringToObject(data, "message", message);
    if (record_id > 0) cJSON_AddNumberToObject(data, "record_id", (double)record_id);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t));
    r->code = 0; r->json = s; return r;
}

result_t *result_err(const char *op, const char *uid, int code, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "op", op);
    if (uid) cJSON_AddStringToObject(root, "uid", uid);
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg ? msg : "");
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t));
    r->code = code; r->json = s; return r;
}

char *output_to_json(result_t *r) {
    if (!r || !r->json) return NULL;
    cJSON *root = cJSON_Parse(r->json);
    if (root) {
        time_t t = time(NULL);
        struct tm tm;
        char buf[32];
        localtime_r(&t, &tm);
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
        cJSON_AddStringToObject(root, "timestamp", buf);
        char *s = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (s) return s;
    }
    /* fallback: 返回原始 json 副本(可能非合法 JSON,但保证非空) */
    return strdup(r->json);
}

void output_print(result_t *r) {
    char *s = output_to_json(r);
    if (s) { printf("%s\n", s); free(s); }
}

void result_free(result_t *r) {
    if (!r) return;
    free(r->json);
    free(r);
}
