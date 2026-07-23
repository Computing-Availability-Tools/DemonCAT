/* src/injectors/injectors.c */
#include "injector.h"
#include <string.h>

const injector_t *const builtin_injectors[] = {
    /* empty — all faults go through cnf+script path */
};

const int builtin_injector_count =
    sizeof(builtin_injectors) / sizeof(builtin_injectors[0]);

const injector_t *injector_find(const char *uid) {
    for (int i = 0; i < builtin_injector_count; i++) {
        if (strcmp(builtin_injectors[i]->uid, uid) == 0)
            return builtin_injectors[i];
    }
    return NULL;
}
