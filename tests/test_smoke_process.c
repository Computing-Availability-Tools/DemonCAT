/* tests/test_smoke_process.c — Tier 3: real execution tests for process faults */
#include "core/config.h"
#include "core/registry.h"
#include "core/state.h"
#include "core/executor.h"
#include "core/output.h"
#include "core/dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define CK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", #cond); return 1; } } while (0)

static void smoke_setup(void) {
    config_t cfg;
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_init("/tmp/dcat_smoke_proc.json");
    state_load();
    executor_set_mock(NULL);
}

static void smoke_teardown(void) {
    state_init("");
    unlink("/tmp/dcat_smoke_proc.json");
}

int main(void) {
    smoke_setup();

    /* ---- rPROC_exit (inject-only, kill -9) ---- */
    {
        /* spawn a dummy process to kill */
        pid_t pid = fork();
        if (pid == 0) { sleep(60); _exit(0); }
        CK(pid > 0);
        char pidstr[16]; snprintf(pidstr, sizeof pidstr, "%d", pid);

        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "pid"); strcpy(p.items[0].value, pidstr); p.count = 1;

        result_t *r = dispatch_inject("rPROC_exit", &p);
        CK(r && r->code == 0);
        CK(strstr(r->json, "record_id") == NULL);  /* inject-only: no state */
        result_free(r);

        sleep(1);
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        CK(ret == pid);  /* child was killed (zombie reaped) */
        CK(WIFSIGNALED(status));  /* killed by signal (SIGKILL) */
    }

    /* ---- rPROC_hang (SIGSTOP / SIGCONT) ---- */
    {
        pid_t pid = fork();
        if (pid == 0) { sleep(60); _exit(0); }
        CK(pid > 0);
        char pidstr[16]; snprintf(pidstr, sizeof pidstr, "%d", pid);

        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "pid"); strcpy(p.items[0].value, pidstr); p.count = 1;

        result_t *r = dispatch_inject("rPROC_hang", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        /* check process is stopped (T state) */
        char path[64]; snprintf(path, sizeof path, "/proc/%d/status", pid);
        FILE *f = fopen(path, "r");
        CK(f);
        char line[256]; int stopped = 0;
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, "State:") && strchr(line, 'T')) stopped = 1;
        }
        fclose(f);
        CK(stopped);

        r = dispatch_clean("rPROC_hang", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        /* process should be running again */
        CK(kill(pid, 0) == 0);
        kill(pid, 9);
        waitpid(pid, NULL, 0);  /* reap to avoid zombie interfering with zstate test */
    }

    /* ---- rPROC_zstate (zombie) ---- */
    {
        params_t p; memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "count"); strcpy(p.items[0].value, "3"); p.count = 1;

        result_t *r = dispatch_inject("rPROC_zstate", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(2);
        /* check zombies exist */
        char cmd[128]; snprintf(cmd, sizeof cmd, "ps -eo stat | grep '^Z' | wc -l");
        FILE *f = popen(cmd, "r");
        CK(f);
        int n = 0; fscanf(f, "%d", &n); pclose(f);
        CK(n > 0);

        r = dispatch_clean("rPROC_zstate", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        /* zombies should be gone */
        snprintf(cmd, sizeof cmd, "ps -eo stat | grep '^Z' | wc -l");
        f = popen(cmd, "r");
        CK(f);
        n = 0; fscanf(f, "%d", &n); pclose(f);
        CK(n == 0);
    }

    smoke_teardown();
    printf("test_smoke_process: 3 faults passed\n");
    return 0;
}
