/* src/core/executor.c */
#include "executor.h"
#include "output.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static mock_fn g_mock = NULL;

int executor_check_tool(const char *path) {
    return (path && access(path, X_OK) == 0) ? 0 : -1;
}

char *executor_build_cmd(const fault_def_t *f, const char *op, const params_t *p,
                         char *buf, size_t len) {
    (void)op; (void)p;
    if (!f || !buf || len == 0) return NULL;
    strncpy(buf, f->script, len - 1);
    buf[len - 1] = '\0';
    return buf;
}

static void env_name(const char *k, char *out, size_t cap) {
    static const char prefix[] = "DCAT_PARAM_";
    size_t i = 0;
    for (const char *p = prefix; *p && i + 1 < cap; p++) out[i++] = *p;
    for (size_t j = 0; k[j] && i + 1 < cap; j++) {
        char c = k[j];
        if (isalnum((unsigned char)c) || c == '_') out[i++] = (char)toupper((unsigned char)c);
        else out[i++] = '_';
    }
    out[i] = '\0';
}

void executor_set_env(const char *op, const char *uid, const params_t *p) {
    setenv("DCAT_OP", op ? op : "", 1);
    setenv("DCAT_UID", uid ? uid : "", 1);
    if (!p) return;
    for (int i = 0; i < p->count; i++) {
        char ek[80];
        env_name(p->items[i].key, ek, sizeof ek);
        setenv(ek, p->items[i].value, 1);
    }
}

result_t *executor_run(const char *cmd) {
    if (g_mock) return g_mock(cmd);
    if (!cmd) return result_err("run", "", DCAT_E_RUN, "null cmd");

    int p[2];
    if (pipe(p)) return result_err("run", "", DCAT_E_RUN, "pipe failed");
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]); close(p[1]);
        return result_err("run", "", DCAT_E_RUN, "fork failed");
    }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(p[1]);

    char buf[8192];
    size_t total = 0;
    int status = 0;
    while (1) {
        ssize_t r = read(p[0], buf + total, sizeof buf - 1 - total);
        if (r > 0) {
            total += (size_t)r;
            if (total >= sizeof buf - 1) total = sizeof buf - 2;
        } else if (r == 0) {
            break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    close(p[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

    while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r')) buf[--total] = '\0';
    buf[total] = '\0';

    if (WIFEXITED(status)) {
        int ec = WEXITSTATUS(status);
        if (ec == 0) {
            cJSON *data = cJSON_CreateObject();
            if (total > 0) cJSON_AddStringToObject(data, "message", buf);
            return result_ok("run", "", data);
        }
        char emsg[256];
        char snippet[200];
        if (total > 0) {
            size_t sn = total < sizeof snippet - 1 ? total : sizeof snippet - 1;
            memcpy(snippet, buf, sn);
            snippet[sn] = '\0';
        }
        snprintf(emsg, sizeof emsg, "script exit %d%s%s", ec,
                 total > 0 ? ": " : "", total > 0 ? snippet : "");
        return result_err("run", "", DCAT_E_RUN, emsg);
    }
    return result_err("run", "", DCAT_E_RUN, "script signaled");
}

int executor_run_raw(const char *cmd) {
    if (g_mock) return 0;
    if (!cmd) return -1;
    int status = system(cmd);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

void executor_set_mock(mock_fn fn) { g_mock = fn; }
