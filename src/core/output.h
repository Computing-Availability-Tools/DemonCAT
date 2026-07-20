#ifndef DCAT_OUTPUT_H
#define DCAT_OUTPUT_H

#include "types.h"
#include "cJSON.h"

/* Build a success result. `data` (cJSON*) is consumed (freed) by the call;
   pass NULL for an empty data object. */
result_t *result_ok(const char *op, const char *uid, cJSON *data);

/* Build an error result. `code` is a DCAT_E_* constant (also the exit code). */
result_t *result_err(const char *op, const char *uid, int code, const char *msg);

/* Print result JSON to stdout (trailing newline). */
void output_print(const result_t *r);

/* Free a result (json string + struct). NULL-safe. */
void result_free(result_t *r);

#endif /* DCAT_OUTPUT_H */
