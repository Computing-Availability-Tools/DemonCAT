#ifndef DCAT_DISPATCH_H
#define DCAT_DISPATCH_H

#include "types.h"

/* list: emit the registered fault catalog as JSON. */
result_t *dispatch_list(void);

/* query: without uid → list active state records; with uid → run script's
 * query branch to verify the fault is actually active on the system. User params
 * (where k=v) are passed to the script via env vars; script's raw stdout/stderr
 * is printed before a "---" separator, then the JSON result. */
result_t *dispatch_query(const char *uid, const params_t *p);

/* inject: precheck + execute (sync/background) + state record. Assumes already confirmed. */
result_t *dispatch_inject(const fault_def_t *f, const params_t *p);

/* clean: kill (background) or run script clean (sync) + mark inactive. */
result_t *dispatch_clean(const fault_def_t *f, const params_t *p);

/* auto-clean callback: clean one record (used by state auto-clean thread). */
void dispatch_clean_record(const injection_record_t *rec);

/* Build the auto-recovery reaper command line for a duration-bound inject.
 * The spawned sh runs: sleep <dur>; <exe> "clean <uid>" --config <cfg> --yes
 * (uid MUST be quoted so the shell passes `clean <uid>` as a single argv to
 * dcat — otherwise main.c's argv loop keeps only the last non-flag arg). */
void dispatch_build_reaper(const char *exe, const char *uid, const char *cfgpath,
                           int dur, char *buf, size_t len);

#endif /* DCAT_DISPATCH_H */
