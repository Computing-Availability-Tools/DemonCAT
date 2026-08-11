#ifndef DCAT_DISPATCH_H
#define DCAT_DISPATCH_H
#include "types.h"
/* op∈{inject,clean,query,list}；list/query �?uid �?uid 可为 NULL�? * force!=0 �?inject 同资源重叠走原子替换(clean �?+ inject �?；默�?force=0 拒绝重叠�?*/
result_t *dispatch_route_force(const char *uid, const char *op, const params_t *params, int force);
result_t *dispatch_route(const char *uid, const char *op, const params_t *params);  /* force=0 */
/* clean --all：遍历全部注册故�?cnf)执行无参 clean，聚合结果；stateless，不依赖 state.json */
result_t *dispatch_clean_all(void);
#endif
