#ifndef DCAT_CONFIG_H
#define DCAT_CONFIG_H
#include "types.h"
#define DCAT_MAX_FAULTS 128
typedef struct {
    char state_file[256];
    char log_level[16];
    fault_def_t faults[DCAT_MAX_FAULTS];
    int fault_count;
} config_t;
int  config_load(const char *path, config_t *cfg);                       /* 失败返回 -1 */
const fault_def_t *config_find(const config_t *cfg, const char *uid);
void resolve_script(const char *root, const char *val, char *dst, int cap);  /* 相对→prepend root；绝对/home/`.` 不变 */
void derive_project_root(const char *cfgpath, char *root, int cap);          /* <root>/config/demoncat.conf → <root>；相对→'.' */
#endif
