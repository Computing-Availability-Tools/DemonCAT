#ifndef DCAT_TYPES_H
#define DCAT_TYPES_H
#include <stddef.h>

#define DCAT_MAX_PARAMS 16
#define DCAT_KEY_LEN    32
#define DCAT_VAL_LEN    64
typedef struct { char key[DCAT_KEY_LEN]; char value[DCAT_VAL_LEN]; } param_kv_t;
typedef struct { param_kv_t items[DCAT_MAX_PARAMS]; int count; } params_t;

/* result_t: 输出边界，json/payload 堆分配，调用方 result_free。
 * raw=1 → payload 为已是最终文本(如 list 的表格)，output_print 原样输出，不做 JSON 加工。 */
typedef struct { int code; char *json; int raw; } result_t;

/* fault_def: 由 config.c 从 demoncat.conf 载入；registry 持有表 */
typedef struct {
    char uid[64];
    char module[32];
    char desc[128];
    char script[256];
    char supported_ops[64];      /* "inject" | "inject,clean,query" */
    char inject_required[128];   /* inject 必填参数: "iface,loss_pct" */
    char inject_optional[128];   /* inject 可选参数: "direction" */
    char clean_required[128];    /* clean 必填参数: "iface" */
    char clean_optional[128];    /* clean 可选参数 */
    char query_required[128];    /* query 必填参数: "iface" */
    char query_optional[128];    /* query 可选参数 */
} fault_def_t;

/* injection_record_t: state 持有，固定数组 — 仅 inject,clean,query 故障创建 */
typedef struct {
    long long record_id;        /* 单调递增 (64-bit, 防溢出) */
    char uid[64];
    params_t params;            /* inject 时用户提供的参数，用于 clean 按参数匹配 */
    char started_at[20];         /* "YYYY-MM-DD HH:MM:SS" 本地时间 */
    int  active;                /* 1 活跃，0 已清理 */
} injection_record_t;
#define DCAT_MAX_RECORDS 32

/* mock 钩子：捕获 (cmd, env) 不真正执行；返回伪造 result_t（堆分配，调用方 result_free） */
typedef result_t *(*mock_fn)(const char *cmd, const char *const *env);

/* params 辅助 */
void params_init(params_t *p);
int  params_set(params_t *p, const char *key, const char *val);          /* 覆盖更新；满返回 -1 */
const char *params_find(const params_t *p, const char *key);             /* 未找到返回 NULL */
const char *dcat_key_to_env(const char *key);                            /* 返回静态缓冲，DCAT_PARAM_<KEY> */
int  params_match_subset(const params_t *query, const params_t *record); /* query 每个 key 值与 record 一致则 1 */

#endif /* DCAT_TYPES_H */
