#ifndef DCAT_REGISTRY_H
#define DCAT_REGISTRY_H

#include "types.h"

/* Load fault table from config. Returns 0 on success. */
int registry_init(const config_t *cfg);

/* Find a fault by uid. Returns NULL if not found. */
const fault_def_t *registry_find(const char *uid);

/* List all registered faults. Returns pointer to table; *count set. */
const fault_def_t *registry_list(int *count);

#endif /* DCAT_REGISTRY_H */
