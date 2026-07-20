#ifndef DCAT_TYPES_H
#define DCAT_TYPES_H

#include <sys/types.h>
#include <time.h>

/* ---- error / exit codes (result_t.code doubles as process exit code) ---- */
#define DCAT_E_OK       0
#define DCAT_E_RUN      1   /* runtime error */
#define DCAT_E_PARSE    2   /* parse error */
#define DCAT_E_SAFETY   3   /* precheck / safety rejection */
#define DCAT_E_NOTFOUND 4   /* uid not found */

/* ---- limits ---- */
#define DCAT_MAX_PARAMS 16
#define DCAT_KEY_LEN    32
#define DCAT_VAL_LEN    64
#define DCAT_MAX_FAULTS 64
#define DCAT_MAX_RECORDS 32

/* ---- safety ---- */
typedef enum {
    SAFETY_NORMAL = 0,
    SAFETY_WARNING,
    SAFETY_DANGEROUS
} safety_level_t;

/* ---- exec mode ---- */
typedef enum {
    EXEC_SYNC = 0,
    EXEC_BACKGROUND
} exec_mode_t;

/* ---- params (stack) ---- */
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
    char required_params[128];   /* "cores" — precheck validates these non-empty */
    char optional_params[128];   /* "duration" — v0.2: not validated, script handles default */
    safety_level_t safety;
    exec_mode_t   exec_mode;
    int timeout;                 /* catalog-level hint; v0.2 actual driven by duration param (SPEC §7.2) */
} fault_def_t;

/* ---- injection record (state) ---- */
typedef struct {
    int  record_id;             /* monotonic */
    char uid[64];
    pid_t bg_pid;               /* background pid dcat supervises, 0 = sync */
    time_t started_at;
    time_t expires_at;          /* 0 = no auto expiry */
    int active;                 /* 1 active, 0 cleaned */
} injection_record_t;

/* ---- parsed command ---- */
typedef struct {
    char op[16];
    char uid[64];
    params_t params;
} parsed_cmd_t;

/* ---- runtime config ---- */
typedef struct {
    char state_file[256];
    char log_level[16];
    fault_def_t faults[DCAT_MAX_FAULTS];
    int fault_count;
} config_t;

#endif /* DCAT_TYPES_H */
