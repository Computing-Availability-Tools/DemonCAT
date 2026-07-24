#ifndef DCAT_CLI_H
#define DCAT_CLI_H
#include "types.h"

typedef struct {
    const char *op;        /* 子命令 inject|clean|query|list；无则 NULL */
    char uid[64];
    params_t params;
    int  help;             /* --help 出现过 */
    const char *config;    /* --config <path> 值（可选） */
    const char *plugins;   /* --plugins <dir> 值（可选） */
} parsed_cmd_t;

/* 解析 argv：argv[1]=子命令(可选), argv[2]=uid(可选), 剩余 --key=value。
 * --config/--plugins/--help 为全局选项，分别记入 config/plugins/help，不进 params。
 * argv[1] 非子命令时 op=NULL（仅全局选项或空）。返回 0 成功，非 0 解析错误。 */
int cli_parse(int argc, char **argv, parsed_cmd_t *out);

/* argv 中是否出现 "--help"（任意位置，独立于 cli_parse 成败） */
int cli_has_help(int argc, char **argv);

/* argv[1] 若为子命令则返回其指针，否则 NULL */
const char *cli_subcommand(int argc, char **argv);

#endif
