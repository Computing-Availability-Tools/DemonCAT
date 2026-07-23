#ifndef DCAT_PRECHECK_H
#define DCAT_PRECHECK_H
#include "types.h"
/* 执行 SPEC §4.2 预检 4 步 + 未声明参数拒绝。返回 NULL=通过；非 NULL=失败 result_t(code)。
 * fault=NULL 表示 uid 不在 cnf（code 4）。 */
result_t *precheck(const fault_def_t *f, const char *op, const params_t *params);
int op_in_supported(const char *supported_ops, const char *op);
int required_params_present(const fault_def_t *f, const params_t *params);
int declared_params_only(const fault_def_t *f, const params_t *params);  /* 所有用户提供参数在 required∪optional 中声明 */
#endif
