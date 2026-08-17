# 动态插件扩展层架构设计实现

> **关联文档：** SPEC.md §7（注入器）、DESIGN.md §7（注入器设计实现）

## 1. 概述

DemonCAT dispatch 三层扩展机制，前两层分别为 cnf 数据驱动（脚本）与编译注入器（`builtin_injectors[]` 静态数组），故障扩展的第三层：**动态插件层**（`dlopen` 加载的 `.so`），运行时可插拔，无需重编译 dcat。

三层按优先级回退，未命中返回退出码 4。

## 2. 三层 dispatch 路由

`dispatch_route(uid, op, params)`（`src/core/dispatch.c:149-203`）：

```text
dispatch_route(uid, op, params)
  ├─ op == "list"        → dispatch_list（cnf + 动态插件 納入文本表格）     [dispatch.c:19-55]
  ├─ uid 空（非 list）   → exit 2 "uid required (use 'dcat list' to see available faults)"
  ├─ 第1层 registry_find(uid)   → cnf 数据驱动（precheck + cnf_inject/clean/query raw）
  ├─ 第2层 injector_find(uid)   → 编译注入器（inj->precheck + inj->inject/clean/query + state）
  ├─ 第3层 plugin_find(uid)      → 动态插件 plugin_dispatch                          [dispatch.c:95-147]
  └─ 未命中             → exit 4 "uid '%s' not found in catalog (use 'dcat list' to see available faults)"
```

优先级：cnf（数据驱动，免重编译）> 编译注入器（核心内置）> 动态插件（高级扩展）。

## 3. 插件接口 `src/plugins/plugin.h`

```c
#define DCAT_PLUGIN_ABI_VERSION 1

typedef struct dcat_plugin_t {
    int abi_version;              /* 加载时校验 == DCAT_PLUGIN_ABI_VERSION，不匹配则 dlclose 拒绝 */
    const char *name;            /* 显示名（list 输出作 module 字段） */
    const char *description;     /* 描述（list 输出作 desc 字段） */
    const char *uid;             /* 故障 uid */
    const char *supported_ops;   /* "inject" | "inject,clean,query" */
    const char *inject_required; /* inject 必填参数："iface,delay_ms" */
    const char *inject_optional; /* inject 可选参数 */
    const char *clean_required;  /* clean 必填参数 */
    const char *clean_optional;
    const char *query_required;  /* query 必填参数 */
    const char *query_optional;
    int  (*init)(void);          /* dlopen 后调用：资源初始化；成功返回 0 */
    void (*fini)(void);          /* dlclose 前/进程退出时调用：清理 */
    result_t *(*precheck)(const char *op, const params_t *params);  /* 可选自定义预检，NULL 跳过 */
    result_t *(*inject)(const params_t *params);
    result_t *(*clean)(const params_t *params);   /* inject-only 为 NULL */
    result_t *(*query)(const params_t *params);    /* inject-only 为 NULL */
} dcat_plugin_t;

/* .so 唯一导出入口符号 */
const dcat_plugin_t *dcat_plugin_get(void);
```

要点：

- `abi_version` 在结构首字段，加载时强制校验，不匹配则 `dlclose` + stderr 警告（防 ABI 漂移崩溃）
- **per-op 参数声明**：6 个 `*_required`/`*_optional` 字段与 `fault_def_t`（`src/core/types.h:21-26`）格式统一，precheck 按 op 取对应字段校验，避免 clean 误用 inject 参数
- `init`/`fini` 生命周期钩子（硬件设备打开/连接清理）
- 单插件单故障（一个 `.so` 一个 uid）；多故障演进见 §13
- `.so` 仅导出 `dcat_plugin_get`，返回静态结构指针

## 4. 插件管理器 `src/plugins/plugin_manager.{c,h}`

```c
#define DCAT_MAX_PLUGINS 64

int  plugin_load_dir(const char *dir);            /* 扫描 *.so，dlopen + 版本检查 + init + 注册；返回本次加载数，目录不存在视为 0 */
const dcat_plugin_t *plugin_find(const char *uid); /* 线性扫描，未命中 NULL */
int  plugin_count(void);
const dcat_plugin_t *const *plugin_list(int *count); /* list 输出 */
void plugin_fini(void);                            /* 调每个 fini + dlclose */
```

加载流程（`plugin_manager.c:12-60`）：

1. `opendir(dir)` —— 失败（目录不存在）直接 `return 0`
2. `readdir` 循环，跳过非 `*.so`
3. `snprintf` 拼路径 → `dlopen(path, RTLD_NOW | RTLD_LOCAL)`
4. `dlsym(h, "dcat_plugin_get")` —— 缺符号 `dlclose` 拒绝
5. `get()` —— 返回 NULL `dlclose` 拒绝
6. 校验 `p->abi_version == DCAT_PLUGIN_ABI_VERSION` —— 不匹配 `dlclose` 拒绝
7. `p->init && p->init() != 0` —— init 失败 `dlclose` 拒绝
8. 入 `g_plugins[g_count]` / `g_handles[g_count]`，`g_count++`
9. 容量达 `DCAT_MAX_PLUGINS` 则 `break`

任一步失败：`dlclose` + stderr 警告，继续下一个 `.so`（不中断）。`plugin_fini` 调每个 `fini` + `dlclose`，重置 `g_count=0`。

## 5. precheck 通用化 `src/core/precheck.{c,h}`

通用预检函数接受**字符串**，`fault_def_t` 与 `dcat_plugin_t` 共用：

```c
int op_in_supported(const char *supported_ops, const char *op);                                                    /* 1=op 在 supported_ops 中 */
int required_params_present(const char *required_params, const params_t *params);                                  /* 1=全部必填参数存在 */
int declared_params_only(const char *inject_req, const char *inject_opt,
                         const char *clean_req,  const char *clean_opt,
                         const char *query_req,  const char *query_opt,
                         const params_t *params);                                                                /* 1=所有参数已声明；首次未声明参数写入 g_undeclared_param */
result_t *precheck(const fault_def_t *f, const char *op, const params_t *params);                                  /* fault_def 版本，内部调上述通用函数；NULL=通过 */
```

`precheck(f, op, params)`（`precheck.c:73-96`）流程：op_in_supported → declared_params_only（6 串）→ 按 op 取 `f->*_required` 调 `first_missing_required` → `executor_check_tool`（脚本可执行）。

错误提示具体化：

- `unknown parameter '%s' (not declared for %s)` —— 未声明参数
- `missing required parameter '%s' for %s` —— 缺必填参数
- `op not in supported_ops`
- `script not executable`

## 6. dispatch 集成 `src/core/dispatch.c`

### 6.1 `plugin_dispatch(p, op, params)`（`dispatch.c:95-147`）

```text
1. op_in_supported(p->supported_ops, op)        → 失败 exit 3 "op not in supported_ops"
2. declared_params_only(p->inject_required, p->inject_optional,
                       p->clean_required,  p->clean_optional,
                       p->query_required,  p->query_optional, params)
   → 失败 exit 3 "undeclared param"
3. 按 op 取 op_req：inject→p->inject_required / clean→p->clean_required / query→p->query_required
   required_params_present(op_req, params)      → 失败 exit 3 "missing required params"
4. p->precheck(op, params)（可选）              → 非 NULL 且 code!=0 直接返回
5. 按 op 调函数指针：
   - inject：r = p->inject(params)
              if (r->code==0 && p->clean) { state_add(p->uid, params); cJSON 注入 record_id 到 r->json data }
              return r
   - clean ：if (!p->clean) exit 3
              n = state_find_by_params(p->uid, params, ids, DCAT_MAX_RECORDS)
              n==0 → exit 1 "no active injection"
              for each id: rec = state_find_by_id(ids[i])
                            r = p->clean(&rec->params)    /* 用注入时记录的参数 clean */
                            r->code != 0 → return r
                            state_mark_inactive(ids[i]); result_free(r)
              return result_ok "cleaned"
   - query : if (!p->query) exit 3; return p->query(params)
```

**state 写入条件**：`r->code == 0 && p->clean != NULL`（以 clean 函数存在判定"可恢复"，inject-only 插件不写 state）。
**clean 参数**：用 `state_find_by_params` 匹配活跃记录，对每条记录用**注入时保存的参数**调 `p->clean`（用户 clean 时传的参数仅用于匹配）。

### 6.2 `dispatch_list`（`dispatch.c:19-55`）

list 输出文本表格：

- cnf 故障：`uid` / `module` / `supported_ops`（按逗号拆数组）/ `desc`（非空才输出）
- 动态插件：`uid` / `module`(=`p->name`，空则 `""`) / `supported_ops` / `desc`(=`p->description`，非空才输出)

## 7. main.c 集成 `src/main.c`

启动序列（`main.c:27-88`）：

1. `cli_parse(argc, argv, &pc)` —— 含 `--plugins <dir>` / `--config <path>` 全局选项
2. `--help` 优先输出后退出 0（即使其余参数 malformed）
3. `resolve_cfgpath(pc.config, ...)` —— `pc.config` 覆盖，否则 `readlink("/proc/self/exe")` 推导 `<binary_dir>/../config/demoncat.conf`，再退化 `"config/demoncat.conf"`
4. `config_load(cfgpath, &cfg)` + `registry_init(&cfg)`
5. `state_file`：`cfg.state_file[0] ? cfg.state_file : "~/.demoncat/state.json"`；`~` 前缀展开为 `$HOME`
6. `state_load()`
7. 插件目录：`pc.plugins` 覆盖，否则 `derive_project_root(cfgpath) → <root>` + `/plugins`（`<root>/config/demoncat.conf` → `<root>`，相对路径 → `.`）
8. `plugin_load_dir(plugindir)`
9. `dispatch_route(pc.uid, pc.op, &pc.params)`
10. `output_print(r)` + `result_free(r)` + `state_save()` + `plugin_fini()`
11. `return r ? r->code : 1`

**默认插件目录：** 源根 `<root>/plugins`（无安装布局 `<binary_dir>/../lib/...`，见 §13）。

## 8. CMakeLists.txt

- `src/plugins/plugin_manager.c` 编进 `DCAT_CORE`，随 dcat 二进制与所有测试链接
- `target_link_libraries(dcat PRIVATE cjson Threads::Threads ${CMAKE_DL_LIBS})`
- `dcat_test` 函数同样链接 `${CMAKE_DL_LIBS}`
- 示例插件 `src/plugins/sample/sample_plugin.c` + `src/core/output.c` + `src/core/types.c` 编为 MODULE 库：
    - `OUTPUT_NAME "sample"` / `PREFIX "lib"` / `LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/plugins"`
    - 产物：`plugins/libsample.so`（构建产物，`.gitignore` 忽略 `plugins/*.so`，不入仓）
- `add_dependencies(test_plugin_integration sample_plugin)` —— 集成测试依赖 `.so` 先构建

## 9. 相关集成

### 9.1 `cli.c` / `cli.h`

- `parsed_cmd_t.plugins` 字段（`cli.h:11`）记录 `--plugins <dir>` 值
- `cli_parse` 解析 `--plugins <dir>` 与 `--config <path>` 为全局选项，不进 `params`（`cli.c:54-60`）

### 9.2 `help.c`

- 全局帮助列 `--plugins <dir>`：`--plugins <dir>              指定动态插件目录（默认 <root>/plugins）`（`help.c:128`）
- list 子命令说明：`运行 \`dcat list\` 查看完整故障目录（含动态插件）`（`help.c:141`）
- inject/clean/query 子命令帮助尾部提示：`动态插件故障参数见 \`dcat list\``（`help.c:163`）

## 10. 示例插件 `src/plugins/sample/sample_plugin.c`

可恢复故障 `rSAMPLE_test`（inject/clean/query 三操作，无必填参数）：

```c
static result_t *sample_inject(const params_t *params) { return result_ok("inject", "rSAMPLE_test", 0, "sample injected"); }
static result_t *sample_clean(const params_t *params) { return result_ok("clean", "rSAMPLE_test", 0, "sample cleaned"); }
static result_t *sample_query(const params_t *params) { return result_ok("query", "rSAMPLE_test", 0, "sample confirmed"); }

const dcat_plugin_t *dcat_plugin_get(void) {
    static const dcat_plugin_t p = {
        .abi_version = DCAT_PLUGIN_ABI_VERSION,
        .name = "sample", .description = "sample plugin for integration test",
        .uid = "rSAMPLE_test", .supported_ops = "inject,clean,query",
        .inject_required = "", .inject_optional = "",
        .clean_required = "",  .clean_optional = "",
        .query_required = "",  .query_optional = "",
        .init = NULL, .fini = NULL, .precheck = NULL,
        .inject = sample_inject, .clean = sample_clean, .query = sample_query,
    };
    return &p;
}
```

## 11. 测试策略

| 测试 | 文件 | 覆盖 |
| --- | --- | --- |
| `test_plugin_manager` | `tests/ut/test_plugin_manager.c` | `plugin_find("nope")==NULL`、`plugin_count()==0`；`plugin_load_dir("/tmp/dcat-no-plugins-here-xyz")==0` |
| `test_plugin_integration` | `tests/ut/test_plugin_integration.c` | `plugin_load_dir("plugins")>=1`；`plugin_find("rSAMPLE_test")!=NULL`；`dispatch_route inject` 返回 ok + 含 `record_id` + `state_list_active()==1`；`dispatch_route clean` 返回 ok + `state_list_active()==0` |
| 全 ctest 24 项 | — | precheck 重构保持行为（per-op 字段对齐 fault_def）；cnf/injector/plugin 三级优先级不变 |

CMake 注册（`CMakeLists.txt:45-47`）：

- Tier 0b：`test_plugin_manager`、`test_plugin_integration`
- `test_plugin_integration` 通过 `add_dependencies` 依赖 `sample_plugin` 目标

## 12. 关键决策

1. **ABI 版本门控** —— `abi_version` 首字段强制校验，防 dcat 与插件版本不匹配崩溃
2. **per-op 参数声明** —— 6 字段与 `fault_def_t` 统一，precheck 按 op 取对应 `*_required`，避免 clean 误用 inject 参数
3. **元数据驱动预检** —— `supported_ops` + per-op req/opt 复用通用预检函数，避免每插件重复预检逻辑；插件可选 `precheck` 函数指针覆盖默认预检
4. **生命周期 `init`/`fini`** —— 处理硬件设备/连接资源
5. **以 `clean` 函数存在判定可恢复** —— `plugin_dispatch` inject 成功后仅在 `p->clean != NULL` 时写 state（inject-only 插件不写）
6. **三层优先级** —— cnf（数据驱动）> 编译注入器（内置）> 动态插件（高级扩展）
7. **受信任无沙箱** —— 内部复杂故障扩展，不引入沙箱/权限控制

## 13. 不实现（YAGNI，校准至当前代码）

- 插件依赖声明/加载顺序 —— 按目录扫描顺序加载
- 插件签名/权限验证
- 多故障插件（单 `.so` 多 uid）—— 仍单插件单故障
- 插件热加载/卸载 —— 仅启动时 `plugin_load_dir` + 进程退出时 `plugin_fini`
- 跨平台 —— 仅 Linux/WSL（`dlopen`/`dl` 为 POSIX）
- 安装布局 `<binary_dir>/../lib/demoncat/plugins/` —— 当前仅源根 `<root>/plugins`，无独立安装目录推导
- ABI 不匹配的自动化测试覆盖 —— 仅 `plugin_manager.c:42-47` 代码层校验，无独立测试用例
