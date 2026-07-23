/* tests/test_state.c */
#include "core/state.h"
#include <string.h>
#include <unistd.h>

static params_t make_params(const char *k1, const char *v1, const char *k2, const char *v2) {
    params_t p;
    memset(&p, 0, sizeof p);
    if (k1) { strcpy(p.items[0].key, k1); strcpy(p.items[0].value, v1); p.count = 1; }
    if (k2) { strcpy(p.items[1].key, k2); strcpy(p.items[1].value, v2); p.count = 2; }
    return p;
}

int main(void) {
    state_init("/tmp/dcat_test_state.json");

    /* add a record */
    params_t p1 = make_params("cores", "4", NULL, NULL);
    int id = state_add("rCPU_overload", &p1);
    if (id <= 0) return 1;

    /* find by uid */
    injection_record_t r = state_find("rCPU_overload");
    if (!r.record_id || !r.active) return 1;
    if (strcmp(r.uid, "rCPU_overload")) return 1;
    if (r.params.count != 1 || strcmp(r.params.items[0].key, "cores") || strcmp(r.params.items[0].value, "4")) return 1;

    /* add second record (same uid, different params — concurrent injection) */
    params_t p2 = make_params("cores", "8", NULL, NULL);
    int id2 = state_add("rCPU_overload", &p2);
    if (id2 <= 0 || id2 == id) return 1;
    if (state_count_active() != 2) return 1;

    /* find_by_params: match by uid + cores=4 */
    injection_record_t matches[DCAT_MAX_RECORDS];
    int n = state_find_by_params("rCPU_overload", &p1, matches, DCAT_MAX_RECORDS);
    if (n != 1) return 1;
    if (matches[0].record_id != id) return 1;

    /* find_by_params: match all for uid (empty params) */
    params_t empty = {0};
    n = state_find_by_params("rCPU_overload", &empty, matches, DCAT_MAX_RECORDS);
    if (n != 2) return 1;

    /* find_by_params: no match (wrong param value) */
    params_t p3 = make_params("cores", "99", NULL, NULL);
    n = state_find_by_params("rCPU_overload", &p3, matches, DCAT_MAX_RECORDS);
    if (n != 0) return 1;

    /* mark inactive */
    state_mark_inactive(id);
    if (state_find("rCPU_overload").record_id == 0 && state_count_active() != 1) {
        /* state_find returns first active; after marking id inactive, should find id2 */
    }
    if (state_count_active() != 1) return 1;

    /* save and reload */
    if (state_save()) return 1;
    state_init("/tmp/dcat_test_state.json");
    if (state_load()) return 1;
    /* after reload, rCPU_overload with cores=4 is inactive, cores=8 is active */
    if (state_count_active() != 1) return 1;
    r = state_find("rCPU_overload");
    if (!r.record_id || strcmp(r.params.items[0].value, "8")) return 1;

    /* different uid record */
    params_t p4 = make_params("iface", "eth0", "loss_pct", "5");
    int id4 = state_add("rNET_loss", &p4);
    if (id4 <= 0) return 1;
    n = state_find_by_params("rNET_loss", &p4, matches, DCAT_MAX_RECORDS);
    if (n != 1) return 1;

    unlink("/tmp/dcat_test_state.json");
    return 0;
}
