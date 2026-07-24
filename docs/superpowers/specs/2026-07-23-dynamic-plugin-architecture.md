# 动态插件扩展层架构设计

> **日期：** 2026-07-23
> **状态：** 已批准，待实现
> **关联：** SPEC.md §7（注入器）、DESIGN.md §7（注入器设计实现）

## 1. 背景与动机

DemonCAT 当前的故障扩展机制有两层：

1. **cnf + 脚本**（数据驱动，开闭原则，免重编译）——38 条故障全走这条路径
2. **编译注入器 `injector_t`**（进程内高级扩展点，`builtin_injectors[]` 静态数组，编译时注册需重编译）——本期空数组留位

**痛点：** 注入器是编译时静态注册，新增注入器要改 `src/injectors/injectors.c` + 重编译 dcat，不是真正的"可插拔"。后续需支持更多故障模式（二进制协议、硬件寄存器、进程内状态、精确定时等脚本难胜任的复杂故障），需要一种**运行时可插拔**的扩展机制，让第三方能独立开发、分发故障包而无需重编译 dcat。

## 2. 三层扩展架构

在现有两层之上新增**动态插件层**，dispatch 三级回退：

```
dispatch_route(uid, op, params)
  ├─ 第1层 registry_find(uid)     cnf 数据驱动（脚本）     [现有]
  ├─ 第2层 injector_find(uid)    编译注入器（builtin）    [现有]
  └─ 第3层 plugin_find(uid)       动态插件（dlopen .so）   [新增]
     └─ 未命中 → 退出码 4（not found）
```

**优先级：** cnf 优先（保数据驱动免重编译特性）→ 编译注入器（核心内置）→ 动态插件（高级扩展）。三层并存，按故障复杂度选层。

## 3. 插件接口 `src/plugins/plugin.h`

```c
#define DCAT_PLUGIN_ABI_VERSION 1

typedef struct dcat_plugin_t {
    int abi_version;              /* 加载时校验 == DCAT_PLUGIN_ABI_VERSION，不匹配则拒绝 */
    const char *name;             /* 显示名（list 输出） */
    const char *description;      /* 描述 */
    const char *uid;              /* 故障 uid */
    const char *supported_ops;    /* "inject" | "inject,clean,query" */
    const char *required_params;  /* "iface,delay_ms" */
    const char *optional_params;  /* 可选参数名 */

    int  (*init)(void);           /* dlopen 后调用：资源初始化（打开设备/连接）；成功返回 0 */
    void (*fini)(void);           /* dlclose 前/进程退出时调用：清理 */

    result_t *(*precheck)(const char *op, const params_t *params);  /* 可选自定义预检，NULL 跳过 */
    result_t *(*inject)(const params_t *params);
    result_t *(*clean)(const params_t *params);   /* inject-only 为 NULL */
    result_t *(*query)(const params_t *params);    /* inject-only 为 NULL */
} dcat_plugin_t;

/* .so 唯一导出入口符号 */
const dcat_plugin_t *dcat_plugin_get(void);
```

**设计要点：**
- `abi_version` 在结构首字段，加载时强制校验，不匹配则 dlclose + 报错（防 ABI 漂移崩溃）
- `supported_ops`/`required_params`/`optional_params` 是**元数据字符串**，复用 precheck.c 通用预检函数，与 cnf fault_def 预检逻辑一致
- `init`/`fini` 提供生命周期钩子（硬件设备打开/清理）
- 单插件单故障（YAGNI），未来可演进到多故障（加 `injectors[]` 数组）

## 4. 插件管理器 `src/plugins/plugin_manager.{c,h}`

```c
int  plugin_load_dir(const char *dir);            /* 扫描目录 *.so，dlopen + 版本检查 + init + 注册；返回加载数，-1 错误 */
const dcat_plugin_t *plugin_find(const char *uid); /* 线性扫描已加载插件 */
int  plugin_count(void);
const dcat_plugin_t *const *plugin_list(int *count); /* list 输出 */
void plugin_fini(void);                            /* 调每个 fini + dlclose */
```

**加载流程：** `opendir`/`readdir` 扫描 `*.so` → `dlopen` → `dlsym("dcat_plugin_get")` → 校验 `abi_version` → `init()` → 加入 `dynamic_plugins[]`（容量 `DCAT_MAX_PLUGINS` 64）。任一步失败则 dlclose + stderr 警告，继续下一个（不中断）。

## 5. precheck.c 重构（通用化）

现有 `required_params_present`/`declared_params_only` 接受 `fault_def_t *`。重构为接受**字符串**，使 fault_def 与 plugin 共用通用预检：

```c
int op_in_supported(const char *supported_ops, const char *op);              /* 不变 */
int required_params_present(const char *required_params, const params_t *params);        /* fault_def_t* → const char* */
int declared_params_only(const char *required_params, const char *optional_params, const params_t *params);  /* → 两个字符串 */
result_t *precheck(const fault_def_t *f, const char *op, const params_t *params);       /* fault_def 版本，内部调通用函数 */
```

**不改行为：** `test_precheck.c` 测 `precheck(f, op, params)`（fault_def 版本），函数签名不变，现有 7 个测试保持全绿。

## 6. dispatch.c 集成（三级回退）

新增 `plugin_dispatch`：对动态插件做通用预检（op_in_supported + declared_params_only + inject 时 required_params）+ 可选 `plugin->precheck` + 调函数指针 + state 管理（可恢复写 state，inject-only 不写）。

```c
result_t *dispatch_route(uid, op, params) {
    /* 第1层 cnf */  if (registry_find(uid)) ...
    /* 第2层 编译注入器 */  if (injector_find(uid)) ...
    /* 第3层 动态插件 */  if (plugin_find(uid)) return plugin_dispatch(...);
    return result_err(op, uid, 4, "not found");
}
```

**list 输出：** `dispatch_list` 纳入动态插件（uid + name + supported_ops + desc）。

## 7. main.c 集成

启动时 `plugin_load_dir(<plugin_dir>)`。插件目录定位：
- 默认：`<binary_dir>/../lib/demoncat/plugins/`（安装布局，通过 `/proc/self/exe` 解析）
- 开发：源根 `plugins/`（ctest WORKING_DIRECTORY）
- 覆盖：`--plugins <dir>` 全局选项

## 8. CMakeLists.txt

- `src/plugins/plugin_manager.c` 编进 dcat 二进制（DCAT_CORE 增项）
- `target_link_libraries(dcat PRIVATE ... ${CMAKE_DL_LIBS})`
- 示例插件 `src/plugins/sample/sample_plugin.c` 编译为 `plugins/libsample.so`（MODULE 库，不编进 dcat）

## 9. 测试策略

| 测试 | 覆盖 |
|---|---|
| `test_plugin_manager.c` | plugin_find 未命中、abi 不匹配拒绝、空目录加载 0 |
| 示例插件 `sample_plugin.c` | mock inject 返回 ok，验证 plugin_find 命中 + dispatch 路由 + state 写入 |
| 现有 11 个测试 | 不破坏（precheck 重构保持行为，dispatch 三级回退 cnf/injector 优先级不变） |

## 10. 关键决策

1. **ABI 版本门控** —— 防 dcat 与插件版本不匹配崩溃
2. **元数据驱动预检** —— plugin 含 supported_ops/required_params/optional_params，复用通用预检函数，避免每插件重复预检逻辑
3. **生命周期 init/fini** —— 处理硬件设备/连接资源
4. **单插件单故障（YAGNI）** —— 可平滑演进到多故障
5. **三层优先级** —— cnf（数据驱动）> 编译注入器（内置）> 动态插件（高级扩展）
6. **受信任无需沙箱** —— 插件用于内部复杂故障扩展，不引入沙箱/权限控制（YAGNI）

## 11. 不实现（YAGNI）

- 插件依赖声明/加载顺序
- 插件签名/权限验证
- 多故障插件（单 .so 多 uid）
- 插件热加载/卸载（仅启动时加载）
- 跨平台（仅 Linux/WSL，dlopen/dl 是 POSIX）
