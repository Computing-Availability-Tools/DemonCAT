#ifndef DCAT_DISPATCH_H
#define DCAT_DISPATCH_H
#include "types.h"
/* op∈{inject,clean,query,list}；list/query 无 uid 时 uid 可为 NULL */
result_t *dispatch_route(const char *uid, const char *op, const params_t *params);
#endif
