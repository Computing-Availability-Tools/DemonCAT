/* src/core/precheck.h */
#ifndef DCAT_PRECHECK_H
#define DCAT_PRECHECK_H

#include "types.h"

/* Precheck (SPEC §4.2): op supported, required params present (inject),
 * script executable, no unknown params.
 * Returns result_t with code 0 on pass, DCAT_E_PRECHECK on rejection. */
result_t *precheck(const fault_def_t *f, const char *op, const params_t *p);

#endif /* DCAT_PRECHECK_H */
