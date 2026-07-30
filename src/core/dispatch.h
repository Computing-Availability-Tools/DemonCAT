#ifndef DCAT_DISPATCH_H
#define DCAT_DISPATCH_H
#include "types.h"
/* op∈{inject,clean,query,list}；list/query 无 uid 时 uid 可为 NULL。
 * force!=0 时 inject 同资源重叠走原子替换(clean 旧 + inject 新)；默认 force=0 拒绝重叠。 */
result_t *dispatch_route_force(const char *uid, const char *op, const params_t *params, int force);
result_t *dispatch_route(const char *uid, const char *op, const params_t *params);  /* force=0 */
#endif
