#ifndef DCAT_PLUGIN_H
#define DCAT_PLUGIN_H
#include "core/types.h"

#define DCAT_PLUGIN_ABI_VERSION 1

/* 动态插件接口（dlopen .so）。.so 须导出 dcat_plugin_get() 返回此结构指针。
 * abi_version 必须等于 DCAT_PLUGIN_ABI_VERSION，否则加载器拒绝。 */
typedef struct dcat_plugin_t {
    int abi_version;              /* 加载时校验 */
    const char *name;            /* 显示名（list 输出） */
    const char *description;
    const char *uid;             /* 故障 uid */
    const char *supported_ops;   /* "inject" | "inject,clean,query" */
    const char *inject_required; /* inject 必填参数 */
    const char *inject_optional; /* inject 可选参数 */
    const char *clean_required;  /* clean 必填参数 */
    const char *clean_optional;  /* clean 可选参数 */
    const char *query_required;  /* query 必填参数 */
    const char *query_optional;  /* query 可选参数 */
    int  (*init)(void);          /* dlopen 后调用：资源初始化；成功返回 0 */
    void (*fini)(void);          /* dlclose 前/进程退出时调用：清理 */
    result_t *(*precheck)(const char *op, const params_t *params);  /* 可选，NULL 跳过 */
    result_t *(*inject)(const params_t *params);
    result_t *(*clean)(const params_t *params);   /* inject-only 为 NULL */
    result_t *(*query)(const params_t *params);    /* inject-only 为 NULL */
} dcat_plugin_t;

/* .so 唯一导出入口符号 */
const dcat_plugin_t *dcat_plugin_get(void);

#endif
