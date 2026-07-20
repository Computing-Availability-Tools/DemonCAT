#include "core/executor.h"
#include "core/output.h"

#include <stdlib.h>
#include <string.h>

static char last_cmd[256];
static int mock_called;

static result_t *mock(const char *cmd) {
    strncpy(last_cmd, cmd, sizeof last_cmd - 1);
    last_cmd[sizeof last_cmd - 1] = '\0';
    mock_called = 1;
    return result_ok("inject", "x", NULL);
}

int main(void) {
    fault_def_t f;
    memset(&f, 0, sizeof f);
    strcpy(f.script, "/usr/lib/demoncat/scripts/cpu_overload.sh");
    params_t p;
    memset(&p, 0, sizeof p);
    p.count = 2;
    strcpy(p.items[0].key, "cores");
    strcpy(p.items[0].value, "4");
    strcpy(p.items[1].key, "duration");
    strcpy(p.items[1].value, "60");

    char buf[256];
    executor_build_cmd(&f, "inject", &p, buf, sizeof buf);
    if (strcmp(buf, "/usr/lib/demoncat/scripts/cpu_overload.sh")) return 1;

    executor_set_env("inject", "rCPU_overload", &p);
    if (!getenv("DCAT_OP") || strcmp(getenv("DCAT_OP"), "inject")) return 1;
    if (!getenv("DCAT_UID") || strcmp(getenv("DCAT_UID"), "rCPU_overload")) return 1;
    if (!getenv("DCAT_PARAM_CORES") || strcmp(getenv("DCAT_PARAM_CORES"), "4")) return 1;
    if (!getenv("DCAT_PARAM_DURATION") || strcmp(getenv("DCAT_PARAM_DURATION"), "60")) return 1;

    executor_set_mock(mock);
    mock_called = 0;
    result_t *r = executor_run(buf, 1000);
    if (!mock_called || r->code != 0) return 1;
    result_free(r);

    mock_called = 0;
    pid_t pid = executor_spawn(buf);
    if (!mock_called || pid != 1) return 1;
    if (executor_kill(pid) != 0) return 1;

    return 0;
}
