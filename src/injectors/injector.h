/*
 * injector.h — ADVANCED EXTENSION POINT (not built/used in P0).
 *
 * The default, recommended way to add a fault is the cnf/script path
 * (see SPEC.md §4-6 and README "Adding a fault"). This header is reserved for
 * the rare fault that needs in-process custom logic (precise timing, binary
 * protocols) and cannot be expressed as an external script.
 *
 * When implemented, a compiled injector would declare:
 *
 *   const injector_t g_my_fault = {
 *       .uid = "my_fault", .module = "...", .desc = "...",
 *       .safety = SAFETY_WARNING, .supported_ops = "inject,clean,query",
 *       .required_params = "...",
 *       .precheck = ..., .inject = ..., .clean = ..., .query = ...
 *   };
 *
 * and registry would consult a `builtin_injectors[]` array as a fallback when
 * a uid is not found in the cnf catalog.
 */
#ifndef DCAT_INJECTOR_H
#define DCAT_INJECTOR_H

#include "types.h"

typedef struct injector {
    const char *uid;
    const char *module;
    const char *desc;
    safety_level_t safety;
    const char *supported_ops;
    const char *required_params;
    result_t *(*precheck)(const struct injector *self, const params_t *p);
    result_t *(*inject) (const struct injector *self, const params_t *p);
    result_t *(*clean)  (const struct injector *self, const params_t *p);
    result_t *(*query)  (const struct injector *self, const params_t *p);
} injector_t;

#endif /* DCAT_INJECTOR_H */
