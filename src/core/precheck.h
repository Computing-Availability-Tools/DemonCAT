#ifndef DCAT_PRECHECK_H
#define DCAT_PRECHECK_H
#include "types.h"
/* fault_def 版本预检（cnf 故障用）。返回 NULL=通过；非 NULL=失败 result_t(code)。fault=NULL → code 4 */
result_t *precheck(const fault_def_t *f, const char *op, const params_t *params);
/* 通用预检函数（接受字符串，fault_def 与 plugin 共用） */
int op_in_supported(const char *supported_ops, const char *op);
int required_params_present(const char *required_params, const params_t *params);
int declared_params_only(const char *required_params, const char *optional_params, const params_t *params);
#endif
