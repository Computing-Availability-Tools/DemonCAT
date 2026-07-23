/* tests/test_precheck.c */
#include "core/precheck.h"
#include "core/output.h"
#include <string.h>

static fault_def_t make_fault(const char *uid, const char *ops, const char *req,
                              const char *opt, const char *script) {
    fault_def_t f;
    memset(&f, 0, sizeof f);
    strcpy(f.uid, uid);
    strcpy(f.supported_ops, ops);
    strcpy(f.required_params, req);
    strcpy(f.optional_params, opt ? opt : "");
    strcpy(f.script, script);
    return f;
}

static params_t make_params(const char *k1, const char *v1,
                           const char *k2, const char *v2) {
    params_t p;
    memset(&p, 0, sizeof p);
    if (k1) { strcpy(p.items[0].key, k1); strcpy(p.items[0].value, v1); p.count = 1; }
    if (k2) { strcpy(p.items[1].key, k2); strcpy(p.items[1].value, v2); p.count = 2; }
    return p;
}

int main(void) {
    fault_def_t f = make_fault("rCPU_overload", "inject,clean,query", "cores", "", "/bin/true");

    /* inject: required param present → pass */
    params_t ok_p = make_params("cores", "4", NULL, NULL);
    result_t *r = precheck(&f, "inject", &ok_p);
    if (!r || r->code != 0) return 1;
    result_free(r);

    /* inject: required param missing → fail (exit 3) */
    params_t empty_p = {0};
    r = precheck(&f, "inject", &empty_p);
    if (!r || r->code != DCAT_E_PRECHECK) return 1;
    if (!strstr(r->json, "missing required param")) return 1;
    result_free(r);

    /* inject: required param present but empty value → fail */
    params_t emptyval;
    memset(&emptyval, 0, sizeof emptyval);
    strcpy(emptyval.items[0].key, "cores");
    strcpy(emptyval.items[0].value, "");
    emptyval.count = 1;
    r = precheck(&f, "inject", &emptyval);
    if (!r || r->code != DCAT_E_PRECHECK) return 1;
    result_free(r);

    /* clean: op supported → pass (no required param check for clean) */
    r = precheck(&f, "clean", &ok_p);
    if (!r || r->code != 0) return 1;
    result_free(r);

    /* query: op supported → pass */
    r = precheck(&f, "query", &ok_p);
    if (!r || r->code != 0) return 1;
    result_free(r);

    /* op not supported (inject-only fault + clean) */
    fault_def_t f_inject_only = make_fault("rPROC_exit", "inject", "pid", "", "/bin/true");
    r = precheck(&f_inject_only, "clean", &ok_p);
    if (!r || r->code != DCAT_E_PRECHECK) return 1;
    if (!strstr(r->json, "op not supported")) return 1;
    result_free(r);

    r = precheck(&f_inject_only, "query", &ok_p);
    if (!r || r->code != DCAT_E_PRECHECK) return 1;
    result_free(r);

    /* script not executable */
    fault_def_t f_bad_script = make_fault("rX", "inject", "", "", "/nope/nope.sh");
    r = precheck(&f_bad_script, "inject", &empty_p);
    if (!r || r->code != DCAT_E_PRECHECK) return 1;
    if (!strstr(r->json, "script not executable")) return 1;
    result_free(r);

    /* unknown param rejection (all commands) */
    fault_def_t f2 = make_fault("rNET_loss", "inject,clean,query", "iface,loss_pct", "", "/bin/true");
    params_t unknown_p = make_params("iface", "eth0", "bogus_param", "val");
    r = precheck(&f2, "inject", &unknown_p);
    if (!r || r->code != DCAT_E_PRECHECK) return 1;
    if (!strstr(r->json, "unknown param")) return 1;
    result_free(r);

    /* unknown param rejection for clean too */
    r = precheck(&f2, "clean", &unknown_p);
    if (!r || r->code != DCAT_E_PRECHECK) return 1;
    result_free(r);

    /* optional param accepted */
    fault_def_t f3 = make_fault("rNET_degrade", "inject,clean,query", "iface", "speed_mbps", "/bin/true");
    params_t opt_p = make_params("iface", "eth0", "speed_mbps", "10");
    r = precheck(&f3, "inject", &opt_p);
    if (!r || r->code != 0) return 1;
    result_free(r);

    /* optional param only, no required → inject passes without any params */
    fault_def_t f4 = make_fault("rX", "inject,clean,query", "", "speed_mbps", "/bin/true");
    r = precheck(&f4, "inject", &empty_p);
    if (!r || r->code != 0) return 1;
    result_free(r);

    return 0;
}
