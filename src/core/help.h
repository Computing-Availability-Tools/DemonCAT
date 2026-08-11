#ifndef DCAT_HELP_H
#define DCAT_HELP_H

/* 全局帮助文本（堆分配，调用方 free） */
char *help_render_global(void);

/* 子命令帮助文本（堆分配，调用方 free）。
 * op∈{inject,clean,query,list}；uid 可为 NULL/空。
 * inject/clean/query：枚举 registry 中支持该 op 的故障及其必填/可选参数；
 *   若 uid 命中某故障，优先详述该故障并给出参数示例。
 * list：描述目录列出行为，无参数表。 */
char *help_render_subcommand(const char *op, const char *uid);

/* 输出到 stdout */
void help_print_global(void);
void help_print_subcommand(const char *op, const char *uid);

#endif
