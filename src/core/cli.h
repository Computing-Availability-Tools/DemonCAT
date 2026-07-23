/* src/core/cli.h */
#ifndef DCAT_CLI_H
#define DCAT_CLI_H

#include "types.h"

/* Parse argv[1..argc-1] into parsed_cmd_t.
 * Subcommand syntax: dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
 * Returns 0 on success, non-zero on parse error (exit code 2). */
int cli_parse(int argc, char **argv, parsed_cmd_t *out);

#endif /* DCAT_CLI_H */
