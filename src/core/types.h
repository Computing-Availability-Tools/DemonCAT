/* src/core/types.h */
#ifndef DCAT_TYPES_H
#define DCAT_TYPES_H

#include <time.h>

/* ---- error / exit codes ---- */
#define DCAT_E_OK        0
#define DCAT_E_RUN       1   /* runtime error (script fail, fork fail) */
#define DCAT_E_PARSE     2   /* parse error (bad command format) */
#define DCAT_E_PRECHECK  3   /* precheck rejection (op not supported, missing params, unknown params) */
#define DCAT_E_NOTFOUND  4   /* uid not found in catalog */

/* ---- limits ---- */
#define DCAT_MAX_PARAMS  16
#define DCAT_KEY_LEN     32
#define DCAT_VAL_LEN     64
#define DCAT_MAX_FAULTS  64
#define DCAT_MAX_RECORDS 32

/* ---- params (stack-allocated) ---- */
typedef struct {
    char key[DCAT_KEY_LEN];
    char value[DCAT_VAL_LEN];
} param_kv_t;

typedef struct {
    param_kv_t items[DCAT_MAX_PARAMS];
    int count;
} params_t;

/* ---- result (output boundary; json heap-allocated) ---- */
typedef struct {
    int code;        /* 0 success, nonzero error */
    char *json;      /* cJSON serialized, owned, freed by result_free */
} result_t;

/* ---- fault definition (loaded from demoncat.conf) ---- */
typedef struct {
    char uid[64];
    char module[32];
    char desc[128];
    char script[256];
    char supported_ops[64];      /* "inject" | "inject,clean,query" */
    char required_params[128];   /* "iface,loss_pct" */
    char optional_params[128];   /* "speed_mbps" */
} fault_def_t;

/* ---- injection record (state) ---- */
typedef struct {
    int  record_id;             /* monotonic */
    char uid[64];
    params_t params;            /* inject-time user params, for clean matching + script replay */
    time_t started_at;
    int active;                 /* 1 active, 0 cleaned */
} injection_record_t;

/* ---- parsed command (cli output) ---- */
typedef struct {
    const char *op;             /* "inject" / "clean" / "query" / "list" / NULL */
    char uid[64];               /* "" if omitted */
    params_t params;             /* --key=value params */
    char config_path[256];       /* --config path, "" if not specified */
    int help;                    /* 1 if --help */
} parsed_cmd_t;

/* ---- runtime config (config_load output) ---- */
typedef struct {
    char state_file[256];
    char log_level[16];
    fault_def_t faults[DCAT_MAX_FAULTS];
    int fault_count;
} config_t;

#endif /* DCAT_TYPES_H */
