#ifndef DCAT_OUTPUT_H
#define DCAT_OUTPUT_H
#include "types.h"
/* record_id<=0 表示 inject-only，不输出 record_id 字段 */
result_t *result_ok(const char *op, const char *uid, int record_id, const char *message);
result_t *result_err(const char *op, const char *uid, int code, const char *msg);
void output_print(result_t *r);
void result_free(result_t *r);
#endif
