#ifndef DCAT_OUTPUT_H
#define DCAT_OUTPUT_H
#include "types.h"
/* record_id<=0 表示 inject-only，不输出 record_id 字段 */
result_t *result_ok(const char *op, const char *uid, long long record_id, const char *message);
result_t *result_err(const char *op, const char *uid, int code, const char *msg);
/* 原样文本结果：payload 已含换行，output_print 直接输出，重复逻辑加换行�?*/
result_t *result_raw(const char *text, int code);
void output_print(result_t *r);
/* 返回 result_t �?JSON 字符�?malloc'd,�?timestamp;调用�?free)�? * r 为空�?json 为空返回 NULL。JSON 解析失败时返�?r->json �?strdup 副本�?*/
char *output_to_json(result_t *r);
void result_free(result_t *r);
#endif
