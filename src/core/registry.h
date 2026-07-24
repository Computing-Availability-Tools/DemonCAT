#ifndef DCAT_REGISTRY_H
#define DCAT_REGISTRY_H
#include "types.h"
#include "config.h"
void registry_init(const config_t *cfg);
const fault_def_t *registry_find(const char *uid);   /* 未命中返回 NULL（dispatch 回退 injector_find） */
const fault_def_t *registry_list(int *count);
int registry_count(void);
#endif
