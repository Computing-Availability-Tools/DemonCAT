#ifndef DCAT_EXECUTOR_H
#define DCAT_EXECUTOR_H
#include "types.h"
/* cnf 故障 inject/clean 路径：fork/exec+pipe 同步，可选 timer 超时；timeout_ms<=0 不超时。
 * 成功返回 result_ok(op,uid,0,message=脚本 stdout 首行)；失败返回 result_err(code=1)。 */
result_t *executor_run_fault(const fault_def_t *f, const char *op, const params_t *params, int timeout_ms);
/* cnf 故障 query 路径：设置 env 后 system() 直通 stdout（继承终端），返回脚本退出码 */
int executor_run_raw_fault(const fault_def_t *f, const char *op, const params_t *params);
int executor_check_tool(const char *path); /* access/X_OK */
void executor_set_mock(mock_fn fn);
#endif
