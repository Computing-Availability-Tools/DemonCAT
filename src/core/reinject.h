#ifndef DCAT_REINJECT_H
#define DCAT_REINJECT_H
#include "types.h"

#define DCAT_REINJECT_CONFLICT 5 /* error code: 同资源已注入 */

/* 理论上限需覆盖真实主机核数(如 640 核的 aarch64 服务器)。 */
#define DCAT_MAX_CORES 1024
#define DCAT_CORES_BYTES (DCAT_MAX_CORES / 8)   /* 128 字节位图 */

/* cores 规格解析：spec="0,1"|"0-3"|"0,1,4-6"|"0" → bits[DCAT_CORES_BYTES] 位图(核 0-1023)。
 * 成功返回 0，非法(空/非数字/越界/lo>hi/尾随逗号)返回 -1。 */
int cores_parse(const char *spec, unsigned char bits[DCAT_CORES_BYTES]);

/* 集合交集非空 → 1，否则 0。 */
int cores_intersect(const unsigned char a[DCAT_CORES_BYTES], const unsigned char b[DCAT_CORES_BYTES]);

/* 判定 new inject 与已有活动记录的资源重叠。
 * 资源键 = clean_required 各参数；cores 走集合交集，其余精确等；
 * clean_required 为 NULL/空 → 保守策略：同 uid 任意活跃记录都算重叠。
 * out_ids[] 仅写入前 max_ids 个(按 max_ids 截断)；返回重叠总数(可能 > max_ids)。
 * 调用方循环须以 min(返回值, max_ids) 为上限；典型调用 max_ids=DCAT_MAX_RECORDS。 */
int reinject_find_overlap(const fault_def_t *f, const params_t *new_params,
                          long long *out_ids, int max_ids);

/* 泛化版：不依赖 fault_def_t（动态插件 / 注入器无该结构时用）。 */
int reinject_find_overlap_ops(const char *uid, const char *clean_required,
                              const params_t *new_params, long long *out_ids, int max_ids);

#endif
