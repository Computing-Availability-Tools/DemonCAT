#ifndef DCAT_STATE_H
#define DCAT_STATE_H

#include "types.h"

/* Initialize state (clears in-memory table). persist_path may be "" to disable persistence. */
int state_init(const char *persist_path);

/* Load persisted state from disk into the table. */
int state_load(void);

/* Add an active injection record. Returns record_id>0, or 0 if the table is full. */
int state_add(const char *uid, pid_t pid, int timeout_s);

/* Find the first ACTIVE record for uid. Returns a SNAPSHOT copy (by value)
 * taken under the lock — caller can safely read all fields without TOCTOU.
 * Check .record_id != 0 to determine if found (0 = not found). */
injection_record_t state_find(const char *uid);

/* Get the active record at slot idx as a SNAPSHOT copy (under lock).
 * Returns 1 if active (fills *out), 0 if not active or out of range. */
int state_record(int idx, injection_record_t *out);

/* Number of active records. */
int state_count_active(void);

/* Mark a record (by id) inactive and persist. */
void state_mark_inactive(int record_id);

/* Persist the table to disk. Returns 0 on success. */
int state_save(void);

/* ---- auto-clean ---- */
typedef void (*state_clean_cb)(const injection_record_t *rec);
void state_set_clean_cb(state_clean_cb cb);
/* Start the background auto-clean thread (scans every 1s for expired records). */
int state_auto_clean_start(void);

/* Synchronously clean all already-expired active records (call once on startup,
   since the one-shot CLI's background thread does not survive process exit). */
void state_lazy_clean(void);

#endif /* DCAT_STATE_H */
