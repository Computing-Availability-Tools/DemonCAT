#include "injector.h"
#include <string.h>

const injector_t *const builtin_injectors[] = { NULL };
const int builtin_injector_count = 0;

const injector_t *injector_find(const char *uid) {
    for (int i = 0; i < builtin_injector_count; i++) {
        if (strcmp(builtin_injectors[i]->uid, uid) == 0)
            return builtin_injectors[i];
    }
    return NULL;
}
