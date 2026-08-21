#include "executor.h"
#include "output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

static mock_fn g_mock = NULL;
void executor_set_mock(mock_fn fn) { g_mock = fn; }

static const char **build_env(const fault_def_t *f, const char *op, const params_t *p, int *out_n) {
    int cap = p->count + 4;
    const char **env = calloc((size_t)cap, sizeof(char *));
    int n = 0;
    char buf[320];
    snprintf(buf, sizeof(buf), "DCAT_OP=%s", op);
    env[n++] = strdup(buf);
    snprintf(buf, sizeof(buf), "DCAT_UID=%s", f->uid);
    env[n++] = strdup(buf);
    for (int i = 0; i < p->count; i++) {
        snprintf(buf, sizeof(buf), "%s=%s", dcat_key_to_env(p->items[i].key), p->items[i].value);
        env[n++] = strdup(buf);
    }
    env[n] = NULL;
    if (out_n) *out_n = n;
    return env;
}
static void free_env(const char **env, int n) {
    for (int i = 0; i < n; i++) free((void *)env[i]);
    free(env);
}
static void apply_env(const char *const *env, int n) {
    for (int i = 0; i < n; i++) {
        const char *eq = strchr(env[i], '=');
        if (eq) {
            size_t kl = (size_t)(eq - env[i]);
            char *k = malloc(kl + 1);
            memcpy(k, env[i], kl);
            k[kl] = '\0';
            setenv(k, eq + 1, 1);
            free(k);
        }
    }
}

/* Clear stale DCAT_PARAM_* env vars for a fault's declared params (required+optional).
 * Prevents param leakage between records in clean loop when records have different param sets. */
static void clear_stale_env_params(const fault_def_t *f) {
    if (!f) return;
    const char *lists[6] = {f->inject_required, f->inject_optional, f->clean_required, f->clean_optional, f->query_required, f->query_optional};
    for (int li = 0; li < 6; li++) {
        const char *q = lists[li];
        if (!q || !q[0]) continue;
        char buf[128];
        strncpy(buf, q, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *save = NULL;
        char *tok = strtok_r(buf, ",", &save);
        while (tok) {
            const char *env_name = dcat_key_to_env(tok);
            unsetenv(env_name);
            tok = strtok_r(NULL, ",", &save);
        }
    }
}

static pid_t g_timed_pid = 0;
static void on_timeout(union sigval sv) {
    (void)sv;
    if (g_timed_pid > 0) kill(g_timed_pid, SIGKILL);
}

result_t *executor_run_fault(const fault_def_t *f, const char *op, const params_t *p, int timeout_ms) {
    if (timeout_ms <= 0) timeout_ms = 60000;  /* default 60s; 0 = hang forever */
    int nenv = 0;
    const char **env = build_env(f, op, p, &nenv);
    if (g_mock) {
        return g_mock(f->script, env); /* mock 场景 env 供测试检查，不释放 */
    }
    clear_stale_env_params(f);
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        free_env(env, nenv);
        return result_err(op, f->uid, 1, "pipe failed");
    }
    pid_t pid = fork();
    if (pid < 0) {
        free_env(env, nenv);
        close(pipefd[0]);
        close(pipefd[1]);
        return result_err(op, f->uid, 1, "fork failed");
    }
    if (pid == 0) {
        apply_env(env, nenv);
        /* stdin 从 /dev/null 读：脚本的参数经 env 传入，不应读 dcat 的 stdin；
         * 非交互场景（cron/ctest/管道）下 stdin 为空管道，脚本误 read 会永久阻塞。 */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, 0);
            close(devnull);
        }
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[0]);
        close(pipefd[1]);
        execl(f->script, f->script, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    timer_t timer = NULL;
    if (timeout_ms > 0) {
        g_timed_pid = pid;
        struct sigevent sev;
        memset(&sev, 0, sizeof(sev));
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_notify_function = on_timeout;
        timer_create(CLOCK_MONOTONIC, &sev, &timer);
        struct itimerspec its;
        memset(&its, 0, sizeof(its));
        its.it_value.tv_sec = timeout_ms / 1000;
        its.it_value.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        timer_settime(timer, 0, &its, NULL);
    }
    char out[4096] = {0};
    size_t total = 0;
    ssize_t m;
    while (total < sizeof(out) - 1 &&
           (m = read(pipefd[0], out + total, sizeof(out) - 1 - total)) > 0)
        total += (size_t)m;
    char drain[1024];
    while ((m = read(pipefd[0], drain, sizeof(drain))) > 0) { /* drain overflow so child never blocks */
        (void)m;
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (timer) {
        timer_delete(timer);
        g_timed_pid = 0;
    }
    free_env(env, nenv);
    if (timeout_ms > 0 && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
        return result_err(op, f->uid, 1, "script timeout");
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    if (code != 0) return result_err(op, f->uid, 1, out[0] ? out : "script failed");
    char *nl = strchr(out, '\n');
    if (nl) *nl = '\0';
    return result_ok(op, f->uid, 0, out[0] ? out : NULL);
}

int executor_run_raw_fault(const fault_def_t *f, const char *op, const params_t *p) {
    int nenv = 0;
    const char **env = build_env(f, op, p, &nenv);
    if (g_mock) {
        result_t *r = g_mock(f->script, env);
        int code = r ? r->code : 1;
        result_free(r);
        return code; /* mock 场景 env 供测试检查，不释放 */
    }
    clear_stale_env_params(f);
    apply_env(env, nenv);
    int rc = system(f->script);
    free_env(env, nenv);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
}

int executor_check_tool(const char *path) {
    return access(path, X_OK);
}
