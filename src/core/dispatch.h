/* src/core/dispatch.h */
#ifndef DCAT_DISPATCH_H
#define DCAT_DISPATCH_H

#include "types.h"

/* inject: precheck → executor_run → state_add → output_ok (or inject-only: no state) */
result_t *dispatch_inject(const char *uid, const params_t *p);

/* clean: precheck → state_find_by_params → per-record executor_run(stored params) → mark inactive */
result_t *dispatch_clean(const char *uid, const params_t *p);

/* query: no uid → state_list; uid → precheck → executor_run_raw → print separator + JSON */
result_t *dispatch_query(const char *uid, const params_t *p);

/* list: registry_list → output catalog JSON */
result_t *dispatch_list(void);

#endif /* DCAT_DISPATCH_H */
