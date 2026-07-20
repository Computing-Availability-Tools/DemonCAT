#include "core/executor.h"
#include "core/output.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

static mock_fn g_mock = NULL;
#define DCAT_FAKE_PID ((pid_t)1)

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

result_t *executor_run(const char *cmd, int timeout_ms) {
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

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    long add_ns = (timeout_ms > 0 ? timeout_ms : 24L * 3600 * 1000) * 1000000L;
    deadline.tv_sec += add_ns / 1000000000L;
    deadline.tv_nsec += add_ns % 1000000000L;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }

    char buf[8192];
    size_t total = 0;
    int done = 0, killed = 0, status = 0;
    while (!done) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long rem_ms = (deadline.tv_sec - now.tv_sec) * 1000L +
                      (deadline.tv_nsec - now.tv_nsec) / 1000000L;
        if (rem_ms < 0) rem_ms = 0;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(p[0], &fds);
        struct timeval tv = {rem_ms / 1000, (rem_ms % 1000) * 1000};
        int rv = select(p[0] + 1, &fds, NULL, NULL, &tv);
        if (rv < 0) { if (errno == EINTR) continue; break; }
        if (rv == 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec > deadline.tv_sec ||
                (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                killed = 1;
                kill(pid, SIGKILL);
                break;
            }
            continue;
        }
        ssize_t r = read(p[0], buf + total, sizeof buf - 1 - total);
        if (r > 0) {
            total += (size_t)r;
            if (total >= sizeof buf - 1) total = sizeof buf - 2;
        } else if (r == 0) {
            done = 1;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    close(p[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

    while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r')) buf[--total] = '\0';
    buf[total] = '\0';

    if (killed) return result_err("run", "", DCAT_E_RUN, "timeout");
    if (WIFEXITED(status)) {
        int ec = WEXITSTATUS(status);
        if (ec == 0) {
            cJSON *data = cJSON_CreateObject();
            if (total > 0) cJSON_AddStringToObject(data, "message", buf);
            return result_ok("run", "", data);
        }
        char emsg[64];
        snprintf(emsg, sizeof emsg, "script exit %d", ec);
        return result_err("run", "", DCAT_E_RUN, emsg);
    }
    return result_err("run", "", DCAT_E_RUN, "script signaled");
}

int executor_run_raw(const char *cmd) {
    if (g_mock) return 0;   /* mock: always success */
    if (!cmd) return -1;
    int status = system(cmd);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

pid_t executor_spawn(const char *cmd) {
    if (g_mock) { (void)g_mock(cmd); return DCAT_FAKE_PID; }
    if (!cmd) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        /* Detach stdio so background reapers don't scribble on the user's
         * terminal (inject-with-duration spawns a reaper that outlives the
         * foreground dcat; without this, its stdout/stderr still point at the
         * terminal and leak `{"status":...}` mid-prompt). */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, 0);
            dup2(devnull, 1);
            dup2(devnull, 2);
            close(devnull);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    return pid;
}

int executor_kill(pid_t pid) {
    if (g_mock) return 0;
    if (pid <= 0) return -1;
    kill(-pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 100; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid) return 0;
        if (kill(pid, 0) != 0) { waitpid(pid, &status, WNOHANG); return 0; }
        usleep(10000);
    }
    kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return 0;
}

void executor_set_mock(mock_fn fn) { g_mock = fn; }
