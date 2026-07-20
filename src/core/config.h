#ifndef DCAT_CONFIG_H
#define DCAT_CONFIG_H

#include "types.h"

/* Parse demoncat.conf (INI) into cfg. Returns 0 on success, nonzero on error. */
int config_load(const char *path, config_t *cfg);

#endif /* DCAT_CONFIG_H */
