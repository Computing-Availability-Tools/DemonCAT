#ifndef DCAT_INJECTOR_H
#define DCAT_INJECTOR_H
#include "core/types.h"

typedef struct injector_t {
    const char *uid;
    result_t *(*inject)(const params_t *params);
    result_t *(*clean)(const params_t *params); /* inject-only 注入器为 NULL */
    result_t *(*query)(const params_t *params); /* inject-only 注入器为 NULL */
    result_t *(*precheck)(const char *op, const params_t *params);
} injector_t;

extern const injector_t *const builtin_injectors[];
extern const int builtin_injector_count;
const injector_t *injector_find(const char *uid);

#endif
