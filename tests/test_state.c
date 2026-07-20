#include "core/state.h"

#include <string.h>
#include <unistd.h>

int main(void) {
    state_init("/tmp/dcat_test_state.json");
    int id = state_add("rCPU_overload", 1234, 1);
    if (id <= 0) return 1;
    injection_record_t r = state_find("rCPU_overload");
    if (!r.record_id || !r.active || r.bg_pid != 1234) return 1;

    /* a second active record */
    int id2 = state_add("rNET_delay", 0, 0);
    if (id2 <= 0 || id2 == id) return 1;
    if (state_count_active() != 2) return 1;

    state_mark_inactive(id);
    if (state_find("rCPU_overload").record_id) return 1;
    if (state_count_active() != 1) return 1;

    /* iteration via state_record finds the still-active one */
    int found = 0;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        injection_record_t rec;
        if (state_record(i, &rec) && !strcmp(rec.uid, "rNET_delay")) found = 1;
    }
    if (!found) return 1;

    if (state_save()) return 1;

    /* reload into fresh table */
    state_init("/tmp/dcat_test_state.json");
    if (state_load()) return 1;
    /* after reload, the inactive rCPU is preserved but inactive; rNET_delay active */
    if (state_find("rCPU_overload").record_id) return 1;
    if (!state_find("rNET_delay").record_id) return 1;

    unlink("/tmp/dcat_test_state.json");
    return 0;
}
