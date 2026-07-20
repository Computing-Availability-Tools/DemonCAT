#ifndef DCAT_EXECUTOR_H
#define DCAT_EXECUTOR_H

#include "types.h"
#include <sys/types.h>

/* Check a tool/script is executable (X_OK). Returns 0 if ok, nonzero otherwise. */
int executor_check_tool(const char *path);

/* Build the command string for a fault op into buf (script path; params go via env). */
char *executor_build_cmd(const fault_def_t *f, const char *op, const params_t *p, char *buf, size_t len);

/* Set DCAT_OP / DCAT_UID / DCAT_PARAM_<KEY> env vars for the script. */
void executor_set_env(const char *op, const char *uid, const params_t *p);

/* Run a command synchronously with a timeout (ms; <=0 = no timeout).
   Returns result_t; success captures stdout into data.message. */
result_t *executor_run(const char *cmd, int timeout_ms);

/* Run a command synchronously, letting stdout/stderr flow to the parent's
   terminal directly (no pipe capture). Returns the script's exit code
   (0 = success, >0 = script exit code, -1 = internal error). Used by the
   query path where the user wants to see the raw script output (tables,
   multi-line text) before the JSON result. Mock: returns 0. */
int executor_run_raw(const char *cmd);

/* Spawn a command in a new session (background). Returns child pid, -1 on error. */
pid_t executor_spawn(const char *cmd);

/* Kill a background pid: SIGTERM then SIGKILL. Returns 0 on success. */
int executor_kill(pid_t pid);

/* ---- test hook ---- */
typedef result_t *(*mock_fn)(const char *cmd);
/* When a mock is set: executor_run returns mock(cmd); executor_spawn records
   cmd via mock and returns a fake pid (1); executor_kill is a no-op (returns 0). */
void executor_set_mock(mock_fn fn);

#endif /* DCAT_EXECUTOR_H */
