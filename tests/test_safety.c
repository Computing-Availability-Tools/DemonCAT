#include "core/safety.h"

#include <string.h>

int main(void) {
    if (!safety_confirm(SAFETY_NORMAL, "")) return 1;
    if (!safety_confirm(SAFETY_WARNING, "y")) return 1;
    if (safety_confirm(SAFETY_WARNING, "n")) return 1;
    if (!safety_confirm(SAFETY_DANGEROUS, "yes")) return 1;
    if (safety_confirm(SAFETY_DANGEROUS, "y")) return 1;

    fault_def_t f;
    memset(&f, 0, sizeof f);
    strcpy(f.uid, "x");
    strcpy(f.supported_ops, "inject,clean");
    strcpy(f.required_params, "cores");
    strcpy(f.script, "/bin/true");

    params_t ok;
    memset(&ok, 0, sizeof ok);
    ok.count = 1;
    strcpy(ok.items[0].key, "cores");
    strcpy(ok.items[0].value, "4");

    params_t miss;
    memset(&miss, 0, sizeof miss);

    if (safety_precheck(&f, "inject", &miss)->code == 0) return 1;  /* missing params */
    if (safety_precheck(&f, "query", &ok)->code == 0) return 1;     /* op not supported */
    if (safety_precheck(&f, "inject", &ok)->code != 0) return 1;    /* ok */

    /* script not executable */
    fault_def_t bad;
    memset(&bad, 0, sizeof bad);
    strcpy(bad.uid, "y");
    strcpy(bad.supported_ops, "inject");
    strcpy(bad.script, "/nope/nope.sh");
    if (safety_precheck(&bad, "inject", &ok)->code == 0) return 1;

    return 0;
}
