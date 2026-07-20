#ifndef DCAT_CLI_H
#define DCAT_CLI_H

#include "types.h"

/* Parse a single dcat command string into `out`.
   Returns 0 on success, nonzero on parse error.
   Grammar:
     cmd   := "inject" uid "(" keys ")" "values" "(" vals ")"
            | "clean"  uid [ "where" kv+ ]
            | "query" [uid] [ "where" kv+ ]
            | "list"
     kv    := key "=" value   (space-separated)
*/
int cli_parse(const char *input, parsed_cmd_t *out);

#endif /* DCAT_CLI_H */
