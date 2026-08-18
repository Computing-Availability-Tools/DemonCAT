/* tests/ut/test_faults_common.h — shared helpers for mock table-driven fault tests */
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

static const char *g_last_cmd;
static const char *const *g_env;
static int g_mock_called;

static result_t *mock_ok(const char *cmd, const char *const *env) {
    g_last_cmd = cmd;
    g_env = env;
    g_mock_called = 1;
    return result_ok("inject", "mock", 0, "ok");
}

#define CK(cond)                                  \
    do {                                          \
        if (!(cond)) {                            \
            fprintf(stderr, "FAIL: %s\n", #cond); \
            return 1;                             \
        }                                         \
    } while (0)

#define CMD_CONTAINS(str) CK(strstr(g_last_cmd ? g_last_cmd : "", str) != NULL)
#define ENV_EQ(key, val)                              \
    do {                                              \
        char _eb[80];                                 \
        snprintf(_eb, sizeof _eb, "%s=%s", key, val); \
        int _found = 0;                               \
        for (int _i = 0; g_env && g_env[_i]; _i++) {  \
            if (strcmp(g_env[_i], _eb) == 0) {        \
                _found = 1;                           \
                break;                                \
            }                                         \
        }                                             \
        CK(_found);                                   \
    } while (0)
#define MOCK_CALLED CK(g_mock_called)

static void faults_setup(void) {
    config_t cfg;
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_reset();
    state_set_file("/tmp/dcat_test_faults.json");
    state_load();
    executor_set_mock(mock_ok);
    g_mock_called = 0;
    g_last_cmd = NULL;
    g_env = NULL;
}

static void faults_teardown(void) {
    executor_set_mock(NULL);
    state_reset();
    state_set_file("");
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
    params_init(&p);
    struct {
        const char *k, *v;
    } pairs[] = {
        {k1, v1}, {k2, v2}, {k3, v3}, {k4, v4}, {k5, v5}, {k6, v6}};
    for (int i = 0; i < 6 && pairs[i].k; i++)
        params_set(&p, pairs[i].k, pairs[i].v);
    return p;
}

/* Check env array for a param key (DCAT_PARAM_<KEY>). Returns 0=ok, 1=fail. */
static int check_param_env(const char *key, const char *expected) {
    char eb[80];
    snprintf(eb, sizeof eb, "%s=%s", dcat_key_to_env(key), expected);
    int found = 0;
    for (int i = 0; g_env && g_env[i]; i++) {
        if (strcmp(g_env[i], eb) == 0) {
            found = 1;
            break;
        }
    }
    CK(found);
    return 0;
}

#endif /* TEST_FAULTS_COMMON_H */
