#ifndef DCAT_CLI_H
#define DCAT_CLI_H
#include "types.h"
typedef struct { const char *op; char uid[64]; params_t params; } parsed_cmd_t;
/* 解析 argv：argv[1]=subcommand, argv[2]=uid(可选), 剩余 --key=value。
 * --config/--help 为全局选项（main 处理），不进 params。返回 0 成功，非 0 解析错误。 */
int cli_parse(int argc, char **argv, parsed_cmd_t *out);
#endif
