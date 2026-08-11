#ifndef DCAT_STATE_H
#define DCAT_STATE_H
#include "types.h"
void state_reset(void);
long long state_add(const char *uid, const params_t *params);   /* 返回 record_id；满返回 -1 */
int  state_find_by_params(const char *uid, const params_t *query, long long *ids, int max_ids);
const injection_record_t *state_find_by_id(long long id);       /* 仅活跃记�?*/
/* 拷贝 uid 的活跃记录快照到 out（锁内拷贝，调用方无需持锁）；返回拷贝�?<=max)�?*/
int  state_snapshot_by_uid(const char *uid, injection_record_t *out, int max);
int  state_list_active(void);
void state_mark_inactive(long long id);
void state_set_file(const char *path);
void state_save(void);
void state_load(void);
typedef void (*state_visit_fn)(const injection_record_t *r, void *ctx);
void state_for_each_active(state_visit_fn fn, void *ctx);
/* 访问全部已用记录(活跃 + 已清�?record_id>0)。history/审计用�?*/
void state_for_each_all(state_visit_fn fn, void *ctx);
/* state 文件缺失�?JSON 解析失败(损坏/截断)时为真：clean 据此决定是否回退脚本清理 */
int  state_is_lost(void);
#endif
