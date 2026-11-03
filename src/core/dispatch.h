#ifndef DCAT_DISPATCH_H
#define DCAT_DISPATCH_H
#include "types.h"
/* op∈{inject,clean,query,list}；list/query 无 uid 时 uid 可为 NULL。
 * force!=0 时 inject 同资源重叠走原子替换(clean 旧 + inject 新)；默认 force=0 拒绝重叠。 */
result_t *dispatch_route_force(const char *uid, const char *op, const params_t *params, int force);
result_t *dispatch_route(const char *uid, const char *op, const params_t *params); /* force=0 */
/* clean --all：遍历全部注册故障(cnf)执行无参 clean，聚合结果；stateless，不依赖 state.json */
result_t *dispatch_clean_all(void);
/* 回滚一条"已执行但未能记账"的注入：用精确参数直接执行该故障的 clean。
 * 返回 1=已清理，0=失败/无处理器。 */
int dispatch_rollback_record(const char *uid, const params_t *params);
/* 回滚最近一次 state_save 所有未落盘记录，返回回滚条数。 */
int dispatch_rollback_dropped(void);
#endif
