#ifndef DCAT_CLI_H
#define DCAT_CLI_H
#include "types.h"

typedef struct {
    const char *op;        /* 子命�?inject|clean|query|list；无�?NULL */
    char uid[64];
    params_t params;
    int  help;             /* --help 出现�?*/
    int  force;            /* --force 出现�?(�?inject 路径生效) */
    int  all;              /* --all 出现�?(�?clean 路径生效：无�?clean 全部故障) */
    int  port;             /* --port <n> (�?serve); 0=默认 8080 */
    const char *bind;      /* --bind <addr> (�?serve); NULL=0.0.0.0 (全接�? */
    const char *webroot;   /* --webroot <dir> (�?serve); NULL=派生 */
    int  allow_write;      /* --allow-write (�?serve); 0=只读默认,1=开 POST inject/clean */
    const char *config;    /* --config <path> 值（可选） */
    const char *plugins;   /* --plugins <dir> 值（可选） */
} parsed_cmd_t;

/* 解析 argv：argv[1]=子命�?可�?, argv[2]=uid(可�?, 剩余 --key=value�? * --config/--plugins/--help 为全局选项，分别记�?config/plugins/help，不�?params�? * argv[1] 非子命令�?op=NULL（仅全局选项或空）。返�?0 成功，非 0 解析错误�?*/
int cli_parse(int argc, char **argv, parsed_cmd_t *out);

/* argv 中是否出�?"--help"（任意位置，独立�?cli_parse 成败�?*/
int cli_has_help(int argc, char **argv);

/* argv[1] 若为子命令则返回其指针，否则 NULL */
const char *cli_subcommand(int argc, char **argv);

/* 返回上次 parse 错误的具体消息（用于 JSON 输出�?*/
const char *cli_get_error(void);

#endif
