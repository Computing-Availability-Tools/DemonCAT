/* tests/test_faults_common.h — shared helpers for mock table-driven fault tests */
#ifndef TEST_FAULTS_COMMON_H
#define TEST_FAULTS_COMMON_H

#include "core/config.h"
#include "core/registry.h"
#include "core/state.h"
#include "core/executor.h"
#include "core/output.h"
#include "core/dispatch.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_last_cmd[256];
static int  g_mock_called;

static result_t *mock_ok(const char *cmd) {
    strncpy(g_last_cmd, cmd, sizeof g_last_cmd - 1);
    g_last_cmd[sizeof g_last_cmd - 1] = '\0';
    g_mock_called = 1;
    return result_ok("inject", "mock", NULL);
}

#define CK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", #cond); return 1; } } while (0)

#define CMD_CONTAINS(str) CK(strstr(g_last_cmd, str) != NULL)
#define ENV_EQ(key, val) do { \
    const char *_v = getenv(key); \
    CK(_v && strcmp(_v, val) == 0); \
} while (0)
#define MOCK_CALLED CK(g_mock_called)

static void faults_setup(void) {
    config_t cfg;
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_init("/tmp/dcat_test_faults.json");
    state_load();
    executor_set_mock(mock_ok);
    g_mock_called = 0;
    g_last_cmd[0] = '\0';
}

static void faults_teardown(void) {
    executor_set_mock(NULL);
    state_init("");
    unlink("/tmp/dcat_test_faults.json");
}

/* Build params_t from up to 6 key-value pairs (NULL-terminated) */
static params_t mkparams(const char *k1, const char *v1,
                         const char *k2, const char *v2,
                         const char *k3, const char *v3,
                         const char *k4, const char *v4,
                         const char *k5, const char *v5,
                         const char *k6, const char *v6) {
    params_t p;
    memset(&p, 0, sizeof p);
    struct { const char *k, *v; } pairs[] = {
        {k1,v1},{k2,v2},{k3,v3},{k4,v4},{k5,v5},{k6,v6}
    };
    for (int i = 0; i < 6 && pairs[i].k; i++) {
        strncpy(p.items[p.count].key, pairs[i].k, DCAT_KEY_LEN - 1);
        strncpy(p.items[p.count].value, pairs[i].v, DCAT_VAL_LEN - 1);
        p.count++;
    }
    return p;
}

/* Check env var for a param key (DCAT_PARAM_<KEY uppercased>). Returns 0=ok, 1=fail. */
static int check_param_env(const char *key, const char *expected) {
    char env_name[80];
    snprintf(env_name, sizeof env_name, "DCAT_PARAM_%s", key);
    for (char *p = env_name + 12; *p; p++) {
        if (isalnum((unsigned char)*p)) *p = toupper((unsigned char)*p);
        else *p = '_';
    }
    const char *val = getenv(env_name);
    CK(val && strcmp(val, expected) == 0);
    return 0;
}

#endif /* TEST_FAULTS_COMMON_H */
