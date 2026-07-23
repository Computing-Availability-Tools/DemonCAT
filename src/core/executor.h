/* src/core/executor.h */
#ifndef DCAT_EXECUTOR_H
#define DCAT_EXECUTOR_H

#include "types.h"

/* Check a tool/script is executable (X_OK). Returns 0 if ok, nonzero otherwise. */
int executor_check_tool(const char *path);

/* Build the command string for a fault op into buf (script path; params go via env). */
char *executor_build_cmd(const fault_def_t *f, const char *op, const params_t *p,
                         char *buf, size_t len);

/* Set DCAT_OP / DCAT_UID / DCAT_PARAM_<KEY> env vars for the script. */
void executor_set_env(const char *op, const char *uid, const params_t *p);

/* Run a command synchronously (fork/exec + pipe, capture stdout into data.message).
   Returns result_t. If mock is set, calls mock(cmd) instead of fork/exec. */
result_t *executor_run(const char *cmd);

/* Run a command synchronously, stdout/stderr flow to terminal directly (no pipe).
   Returns script exit code (0=success, >0=script exit, -1=internal error).
   Mock: returns 0. Used by query path. */
int executor_run_raw(const char *cmd);

/* ---- test hook ---- */
typedef result_t *(*mock_fn)(const char *cmd);
void executor_set_mock(mock_fn fn);

#endif /* DCAT_EXECUTOR_H */
