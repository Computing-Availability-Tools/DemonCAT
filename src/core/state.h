#ifndef DCAT_STATE_H
#define DCAT_STATE_H
#include "types.h"
void state_reset(void);
int  state_add(const char *uid, const params_t *params);   /* 返回 record_id；满返回 -1 */
int  state_find_by_params(const char *uid, const params_t *query, int *ids, int max_ids);
const injection_record_t *state_find_by_id(int id);       /* 仅活跃记录 */
int  state_list_active(void);
void state_mark_inactive(int id);
void state_set_file(const char *path);
void state_save(void);
void state_load(void);
typedef void (*state_visit_fn)(const injection_record_t *r, void *ctx);
void state_for_each_active(state_visit_fn fn, void *ctx);
#endif
