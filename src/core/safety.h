#ifndef DCAT_SAFETY_H
#define DCAT_SAFETY_H

#include "types.h"

/* Confirmation gate. `answer` is the user input line (no newline).
   NORMAL -> always approved; WARNING -> approved if answer begins y/Y;
   DANGEROUS -> approved only if answer == "yes". Returns 1 approved, 0 rejected. */
int safety_confirm(safety_level_t level, const char *answer);

/* Precheck (SPEC §5.2): op supported, required params present (inject), script executable.
   Returns result_t with code 0 on pass, DCAT_E_SAFETY on rejection. */
result_t *safety_precheck(const fault_def_t *f, const char *op, const params_t *p);

#endif /* DCAT_SAFETY_H */
