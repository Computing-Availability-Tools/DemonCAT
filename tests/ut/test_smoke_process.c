/* tests/ut/test_smoke_process.c — Tier 3: real execution tests for process faults */
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
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

static const char *g_smoke_name = "";

#define CK(cond)                                                            \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s\n", #cond);                           \
            fprintf(stderr, "DCAT_SUBTEST|smoke|%s|FAIL|\n", g_smoke_name); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static void smoke_setup(void) {
    config_t cfg;
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_reset();
    state_set_file("/tmp/dcat_smoke_proc.json");
    state_load();
    executor_set_mock(NULL);
}

static void smoke_teardown(void) {
    state_reset();
    state_set_file("");
    unlink("/tmp/dcat_smoke_proc.json");
}

int main(void) {
    smoke_setup();

    /* ---- rPROC_exit (inject-only, kill -9) ---- */
    g_smoke_name = "rPROC_exit";
    {
        pid_t pid = fork();
        if (pid == 0) {
            sleep(60);
            _exit(0);
        }
        CK(pid > 0);
        char pidstr[16];
        snprintf(pidstr, sizeof pidstr, "%d", pid);

        params_t p;
        memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "pid");
        strcpy(p.items[0].value, pidstr);
        p.count = 1;

        result_t *r = dispatch_route("rPROC_exit", "inject", &p);
        CK(r && r->code == 0);
        CK(strstr(r->json, "record_id") == NULL);
        result_free(r);

        sleep(1);
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        CK(ret == pid);
        CK(WIFSIGNALED(status));
    }
    fprintf(stderr, "DCAT_SUBTEST|smoke|%s|PASS|\n", g_smoke_name);

    /* ---- rPROC_hang (SIGSTOP / SIGCONT) ---- */
    g_smoke_name = "rPROC_hang";
    {
        pid_t pid = fork();
        if (pid == 0) {
            sleep(60);
            _exit(0);
        }
        CK(pid > 0);
        char pidstr[16];
        snprintf(pidstr, sizeof pidstr, "%d", pid);

        params_t p;
        memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "pid");
        strcpy(p.items[0].value, pidstr);
        p.count = 1;

        result_t *r = dispatch_route("rPROC_hang", "inject", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        char path[64];
        snprintf(path, sizeof path, "/proc/%d/status", pid);
        FILE *f = fopen(path, "r");
        CK(f);
        char line[256];
        int stopped = 0;
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, "State:") && strchr(line, 'T')) stopped = 1;
        }
        fclose(f);
        CK(stopped);

        r = dispatch_route("rPROC_hang", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        CK(kill(pid, 0) == 0);
        kill(pid, 9);
        waitpid(pid, NULL, 0);
    }
    fprintf(stderr, "DCAT_SUBTEST|smoke|%s|PASS|\n", g_smoke_name);

    /* ---- rPROC_zstate (kill target -> zombie) ---- */
    g_smoke_name = "rPROC_zstate";
    {
        pid_t watcher = fork();
        CK(watcher >= 0);
        if (watcher == 0) {
            setsid();
            pid_t target = fork();
            if (target == 0) {
                sleep(30);
                _exit(0);
            }
            FILE *f = fopen("/tmp/dcat_zstate_test.pid", "w");
            if (f) {
                fprintf(f, "%d", (int)target);
                fclose(f);
            }
            sleep(30);
            _exit(0);
        }
        sleep(1);

        char target_str[16] = {0};
        FILE *f = fopen("/tmp/dcat_zstate_test.pid", "r");
        CK(f);
        if (fscanf(f, "%15s", target_str) != 1) target_str[0] = '\0';
        fclose(f);
        unlink("/tmp/dcat_zstate_test.pid");

        params_t p;
        memset(&p, 0, sizeof p);
        strcpy(p.items[0].key, "pid");
        strcpy(p.items[0].value, target_str);
        p.count = 1;

        result_t *r = dispatch_route("rPROC_zstate", "inject", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        r = dispatch_route("rPROC_zstate", "clean", &p);
        CK(r && r->code == 0);
        result_free(r);

        sleep(1);
        char cmd[128];
        snprintf(cmd, sizeof cmd, "ls /proc/%s 2>/dev/null | wc -l", target_str);
        f = popen(cmd, "r");
        CK(f);
        int n = 0;
        if (fscanf(f, "%d", &n) != 1) n = 0;
        pclose(f);
        CK(n == 0);
        kill(watcher, 9);
        waitpid(watcher, NULL, 0);
    }
    fprintf(stderr, "DCAT_SUBTEST|smoke|%s|PASS|\n", g_smoke_name);

    smoke_teardown();
    return 0;
}
