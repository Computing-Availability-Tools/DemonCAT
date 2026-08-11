#ifndef DCAT_PLUGIN_MANAGER_H
#define DCAT_PLUGIN_MANAGER_H
#include "plugin.h"
#include <stddef.h>

#define DCAT_MAX_PLUGINS 64

/* 扫描目录 *.so，dlopen + 版本检�?+ init + 注册。返回本次加载数，目录不存在视为 0 */
int  plugin_load_dir(const char *dir);
const dcat_plugin_t *plugin_find(const char *uid);  /* 线性扫描，未命�?NULL */
int  plugin_count(void);
const dcat_plugin_t *const *plugin_list(int *count);  /* list 输出 */
void plugin_fini(void);  /* 调每�?fini + dlclose */
#endif
