#include "core/state.h"

#include <unistd.h>

static int fired = 0;

static void cb(const injection_record_t *r) {
    (void)r;
    fired = 1;
    state_mark_inactive(r->record_id);
}

int main(void) {
    state_init("/tmp/dcat_ac.json");
    state_set_clean_cb(cb);
    if (state_auto_clean_start()) return 1;

    int id = state_add("rT", 0, 1);   /* 1s timeout */
    if (id <= 0) return 1;
    if (!state_find("rT").record_id) return 1;

    sleep(3);                          /* wait past the 1s expiry + a scan tick */
    if (!fired) return 1;
    if (state_find("rT").record_id) return 1;   /* cleaned -> inactive */

    unlink("/tmp/dcat_ac.json");
    return 0;
}
