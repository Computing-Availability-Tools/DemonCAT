/* src/core/state.h */
#ifndef DCAT_STATE_H
#define DCAT_STATE_H

#include "types.h"

/* Initialize state (clears in-memory table). persist_path may be "" to disable persistence. */
int state_init(const char *persist_path);

/* Load persisted state from disk into the table. Returns 0 on success. */
int state_load(void);

/* Add an active injection record. Returns record_id>0, or 0 if the table is full. */
int state_add(const char *uid, const params_t *params);

/* Find active records matching uid + params. If params->count == 0, match all for uid.
 * Fills out[] with snapshots. Returns count of matching records. */
int state_find_by_params(const char *uid, const params_t *params,
                         injection_record_t out[], int max_out);

/* Find the first ACTIVE record for uid. Returns snapshot by value (record_id=0 if not found). */
injection_record_t state_find(const char *uid);

/* Get the active record at slot idx as a snapshot. Returns 1 if active, 0 if not/out of range. */
int state_record(int idx, injection_record_t *out);

/* Number of active records. */
int state_count_active(void);

/* Mark a record (by id) inactive and persist. */
void state_mark_inactive(int record_id);

/* Persist the table to disk. Returns 0 on success. */
int state_save(void);

#endif /* DCAT_STATE_H */
