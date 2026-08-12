# DemonCAT 设计文档 (DESIGN)

> 对应 [SPEC.md](SPEC.md)。描述分层架构、数据结构、模块职责、按模块分组的故障详设、关键流程、脚本契约、注入器设计实现、目录结构、构建、测试设计与命令行场景。

## 目录

- [1. 架构设计](#1-架构设计)
  - [1.1 分层架构](#11-分层架构)
  - [1.2 扩展机制：cnf + 脚本驱动](#12-扩展机制cnf--脚本驱动)
  - [1.3 数据流](#13-数据流)
  - [1.4 与编译注入器的关系](#14-与编译注入器的关系)
- [2. 数据结构](#2-数据结构)
- [3. 模块职责](#3-模块职责)
  - [3.1 main.c](#31-mainc)
  - [3.2 cli.c（命令解析器）](#32-clic命令解析器)
  - [3.3 registry.c](#33-registryc)
  - [3.4 executor.c](#34-executorc)
  - [3.5 precheck.c](#35-precheckc)
  - [3.6 state.c](#36-statec)
  - [3.7 config.c](#37-configc)
  - [3.8 output.c](#38-outputc)
  - [3.9 dispatch.c](#39-dispatchc)
  - [3.10 help.c](#310-helpc)
  - [3.11 reinject.c](#311-reinjectc)
  - [3.12 serve.c](#312-servec)
  - [3.13 plugin_manager.c](#313-plugin_managerc)
- [4. 故障详设（按模块分组）](#4-故障详设按模块分组)
  - [4.1 网络模块（network）](#41-网络模块network)
  - [4.2 进程模块（process）](#42-进程模块process)
  - [4.3 CPU 模块（cpu）](#43-cpu-模块cpu)
  - [4.4 存储模块（storage）](#44-存储模块storage)
  - [4.5 NPU 模块（npu）](#45-npu-模块npu)
- [5. 关键流程](#5-关键流程)
  - [5.1 inject（含 inject-only 分支 + 注入器回退）](#51-inject含-inject-only-分支--注入器回退)
  - [5.2 clean](#52-clean)
  - [5.3 query / list](#53-query--list)
- [6. 脚本契约（实现约定）](#6-脚本契约实现约定)
  - [6.1 通用约定](#61-通用约定)
  - [6.2 可恢复故障脚本](#62-可恢复故障脚本)
  - [6.3 inject-only 故障脚本](#63-inject-only-故障脚本)
  - [6.4 默认值约定](#64-默认值约定)
- [7. 注入器设计实现](#7-注入器设计实现)
  - [7.1 设计动机与定位](#71-设计动机与定位)
  - [7.2 injector_t 接口定义](#72-injector_t-接口定义)
  - [7.3 接口契约](#73-接口契约)
  - [7.4 注册与查找](#74-注册与查找)
  - [7.5 dispatch 路由](#75-dispatch-路由)
  - [7.6 与 cnf+脚本路径的关系](#76-与-cnf脚本路径的关系)
- [8. Reinject 默认拒绝与原子替换（--force）](#8-reinject-默认拒绝与原子替换--force)
  - [8.1 目标与动机](#81-目标与动机)
  - [8.2 适用范围（v1）](#82-适用范围v1)
  - [8.3 资源键](#83-资源键)
  - [8.4 overlap 检测算法](#84-overlap-检测算法)
  - [8.5 cores 解析器](#85-cores-解析器)
  - [8.6 CLI（--force）](#86-cli--force)
  - [8.7 dispatch 路由改动](#87-dispatch-路由改动)
  - [8.8 向后兼容（BREAKING）](#88-向后兼容breaking)
  - [8.9 测试计划（TDD）](#89-测试计划tdd)
  - [8.10 deferred 与约束](#810-deferred-与约束)
- [9. 目录结构](#9-目录结构)
- [10. 构建](#10-构建)
- [11. 测试设计](#11-测试设计)
  - [11.1 mock_executor](#111-mock_executor)
  - [11.2 表驱动](#112-表驱动)
  - [11.3 故障覆盖矩阵](#113-故障覆盖矩阵)
  - [11.4 真实环境冒烟](#114-真实环境冒烟)
- [12. 命令行与使用场景](#12-命令行与使用场景)
  - [12.1 命令结构](#121-命令结构)
  - [12.2 全局参数](#122-全局参数)
  - [12.3 使用场景](#123-使用场景)
- [13. 与 SPEC 的对应](#13-与-spec-的对应)

---

## 1. 架构设计

### 1.1 分层架构

```
        ┌──────────────────────────────────────────────┐
        │ main.c  (流程编排: 读配置→解析→调度→输出)        │
        └───────────────┬──────────────────────────────┘
        │ cli.c (命令解析器, 产出 parsed_cmd_t)
        ▼
   registry.c (从 demoncat.conf 载入 fault_def 表; 按 uid 查找;
               未命中则回退 injector_find, 见 §7)
        │
   ┌────┴─────┬──────────┬──────────┬──────────┐
   ▼          ▼          ▼          ▼          ▼
 executor   precheck   state      config    output
 (§3.4)     (§3.5)     (§3.6)     (§3.7)    (§3.8)
 (run/      (预检4步)  (records)  (INI 解析  (JSON
  run_raw)                        + fault    schema)
                                  表)
        ▼
    dispatch.c (按 op 分发: inject / clean / query / list / serve)  (§3.9)
    ├─ cnf 故障    → executor_run/run_raw 调脚本           (第1层)
    ├─ 注入器故障  → 直接调 injector_t 函数指针 (§7)       (第2层)
    └─ 动态插件    → plugin_dispatch 调 dcat_plugin_t 函数指针 (第3层)
```

**核心层**（编译进二进制，稳定）：`main / cli / registry / executor / precheck / state / config / output / dispatch / help / reinject / serve / plugin_manager`。
**故障层**（可变，数据驱动）：`demoncat.conf` 声明 + 外部脚本（`src/scripts/<module>/*.sh`）。
**扩展点**：`injector_t` 编译注入器作为进程内高级扩展点，设计见 §7（本期 `builtin_injectors[]` 为空，仅留位）。

> 本期所有故障**统一同步阻塞执行**，不区分 background/sync 模式。

### 1.2 扩展机制：cnf + 脚本驱动

核心设计原则：**新增故障只需写脚本 + 在 `demoncat.conf` 加 `[fault.<uid>]` 段**，调度与解析逻辑自动发现，二进制不变。

扩展方式示例（新增一个内存故障）：

```ini
# demoncat.conf
[fault.rMEM_ecc_inject]
module           = memory
desc             = 内存 ECC 错误注入
script           = /usr/lib/demoncat/scripts/memory/mem_ecc_inject.sh
supported_ops    = inject,clean,query
inject_required  = dimm
clean_required   = dimm
query_required   =              # 已弃用（留空）；query 参数放 query_optional
query_optional   = dimm
```

> per-op required/optional 字段：`inject_required` / `inject_optional` / `clean_required` / `clean_optional` / `query_required`（已弃用，留空）/ `query_optional`。空字段可省略（如上例无可选参数则不写 `*_optional`）。**`query` 不强制必填**——无参时脚本展示全部，有参则过滤；query 参数声明在 `query_optional`。

对应脚本放到 `src/scripts/memory/mem_ecc_inject.sh` 并 `chmod +x`。`dcat list` 自动出现新故障，**免重编译**。

### 1.3 数据流

```
  argv (subcommand + uid + flags)
     │
     ▼
  cli_parse  ──→  parsed_cmd_t { op, uid, params }
     │
     ▼
   registry_find(uid)  ──→  fault_def_t *  (cnf 命中)
      │                  └─ 未命中 → injector_find(uid) → injector_t * (§7.4)
      │                                  └─ 未命中 → plugin_find(uid) → dcat_plugin_t * (§3.13)
      │                                                  └─ 仍未命中 → 退出码 4
     ▼
   dispatch_route:
    ├── inject ──→ precheck(op∈supported_ops, inject_required 齐全, 脚本/注入器可执行)
   │              ├─ inject-only (rPROC_exit): 执行 → output_ok (无 state)
   │              └─ 可恢复:
   │                    cnf:    executor_run(script, DCAT_OP=inject) → state_add(uid, params)
   │                    注入器:  inj->inject(params)                    → state_add(uid, params)
   │                    → output_ok(message, record_id)
   ├── clean ──→ precheck(op∈supported_ops) → state_find_by_params(uid, params)
   │              ├─ 无记录 → output_err(1, "no active injection")
   │              └─ 记录:
    │                    cnf:    executor_run(script, DCAT_OP=clean, 传记录存储的 inject 参数)
    │                    注入器:  inj->clean(params)
   │              → state_mark_inactive → output_ok
   ├── query ──→ 无 uid: state_list → output records JSON (不调脚本/注入器)
   │              有 uid: precheck →
   │                    cnf:    executor_run_raw(DCAT_OP=query)
   │                    注入器:  inj->query(params)
   │              → output
   └── list  ──→ registry_list → output catalog 文本表格
```

### 1.4 三层 dispatch 与扩展机制

dispatch 按优先级回退查找故障实现，三层依次：

1. **cnf 数据驱动**（第一层）：`registry_find(uid)` 查 `demoncat.conf` 载入的 `fault_def` 表。命中 → executor 调脚本。**免重编译**。
2. **编译注入器**（第二层）：`injector_find(uid)` 查 `builtin_injectors[]`（`src/injectors/injector.h`，本期为空数组）。命中 → 直接函数调用。需重编译。
3. **动态插件**（第三层）：`plugin_find(uid)` 查 `dlopen` 加载的 `.so`（`src/plugins/plugin_manager.c`）。命中 → `plugin_dispatch` 调函数指针。运行时可插拔，免重编译。

三层均未命中 → 退出码 4。本期所有故障均走第一层（cnf 路径）；注入器接口设计见 §7，动态插件设计见 [Dynamic_Plugin_Implement.md](Dynamic_Plugin_Implement.md)。

---

## 2. 数据结构

```c
/* types.h — 公共类型 */

/* params_t: 栈上, 承载 --key=value 参数 */
#define DCAT_MAX_PARAMS 16
#define DCAT_KEY_LEN    32
#define DCAT_VAL_LEN    64
typedef struct { char key[DCAT_KEY_LEN]; char value[DCAT_VAL_LEN]; } param_kv_t;
typedef struct { param_kv_t items[DCAT_MAX_PARAMS]; int count; } params_t;

/* result_t: 输出边界, json 由 cJSON 堆分配, 调用方 result_free */
typedef struct { int code; char *json; } result_t;

/* fault_def: 由 config.c 从 demoncat.conf 载入; registry 持有表 */
typedef struct {
    char uid[64];
    char module[32];
    char desc[128];
    char script[256];
    char supported_ops[64];      /* "inject" | "inject,clean,query" */
    char inject_required[128];   /* inject 必填: "iface,loss_pct" */
    char inject_optional[128];   /* inject 可选参数名 */
    char clean_required[128];    /* clean 必填: "iface" */
    char clean_optional[128];    /* clean 可选参数名 */
    char query_required[128];    /* 已弃用（解析兼容保留，留空）；query 参数走 query_optional */
    char query_optional[128];    /* query 可选参数名 */
} fault_def_t;

/* injection_record_t: state 持有, 固定数组 — 仅 inject,clean,query 故障创建 */
typedef struct {
    int  record_id;             /* 单调递增 */
    char uid[64];
    params_t params;            /* inject 时用户提供的参数, 用于 clean 时匹配记录 */
    time_t started_at;
    int active;                 /* 1 活跃, 0 已清理 */
} injection_record_t;
#define DCAT_MAX_RECORDS 32
```

> `fault_def_t` 不含 `safety` / `timeout` 字段（本期不实现安全确认、超时自动恢复）。预检按 op 校验对应的 `*_required` 字段（inject 查 `inject_required`、clean 查 `clean_required`；**query 不强制必填**，无参时脚本展示全部），各 `*_optional` 缺省时不报错。`inject`-only 故障不创建 `injection_record_t`。`injection_record_t` 含 `params` 字段（存储 inject 时用户参数，用于 clean 时按参数匹配记录；同 uid 不同资源允许并发，同资源重注入默认拒绝见 §8）。`injection_record_t` 不含 `bg_pid`（统一同步执行，dcat 不托管子进程 pid）。

> 高级扩展点：`injector_t { uid, 4 个函数指针 inject/clean/query/precheck }` 注册到 `builtin_injectors[]`，registry 未在 cnf 命中时回退查找。完整设计见 §7。

---

## 3. 模块职责

### 3.1 main.c
读取配置（`config_load`，固定路径见 SPEC §7）→ 注册 `fault_def` 表到 registry → 读取 argv（subcommand + uid + flags，含 `--config` / `--plugins` / `--help`）→ `cli_parse` → `state_load()` → `plugin_load_dir(plugindir)`（§3.13）→ 按 op 分发：`serve` 走 serve.c（§3.12），其余走 `dispatch_route`（三层回退：cnf → injector → plugin，§1.4）→ `output_print` → `state_save()` + `plugin_fini()` → 返回退出码。

### 3.2 cli.c（命令解析器）
解析 `subcommand uid [flags]`，产出：
```c
typedef struct { const char *op; char uid[64]; params_t params; int force; } parsed_cmd_t;
```
解析流程：
1. `argv[1]` = subcommand（`inject` / `clean` / `query` / `list`）；`--help` 直接输出帮助并退出
2. `argv[2]` = uid（若存在且不以 `--` 开头）；`query` 和 `list` 可省略 uid
3. 剩余 argv = `--key=value` 标志，逐对解析进 `params_t`（`--config` / `--help` 为全局选项，不进 params）
4. `--force` 为布尔 flag（裸出现 → `force=1`，同 `--help`）；`--force=x` 报错 `--force does not take a value`。仅 inject 路径生效；clean / query / list 出现则忽略（兼容历史脚本）。详见 §8.6。

所有参数统一为 `--key=value` 标志，不再区分 inject 的 `(p1,p2) values (v1,v2)` 与 clean/query 的 `where k=v`。

错误返回解析错误 JSON（退出码 2）。

### 3.3 registry.c
- `registry_init(config)`：从 config 载入 `fault_def` 到静态数组。
- `const fault_def_t *registry_find(const char *uid)`：cnf 查找。
- `registry_list()`：返回全部 cnf 故障，供 `list` 命令。
- **三层回退**（§1.4）：`registry_find` 未命中时，dispatch 转 `injector_find(uid)`（§7.4，本期为空），再未命中转 `plugin_find(uid)`（§3.13，动态插件）。

### 3.4 executor.c
```c
result_t *executor_run(const char *cmd);                   /* 同步: fork/exec+pipe, 捕获 stdout/stderr */
int        executor_run_raw(const char *cmd);              /* 同步: system() 直通 stdout, 返回退出码 */
int        executor_check_tool(const char *path);           /* access/X_OK */
void       executor_set_env(const char *op, const char *uid, const params_t *p); /* 设置 DCAT_OP/DCAT_UID/DCAT_PARAM_* */
void       executor_clear_env_params(const fault_def_t *f); /* 清除 fault 声明的 DCAT_PARAM_* 环境变量 */
void       executor_set_mock(mock_fn fn);                   /* 测试钩子 */
```
- 命令构造：`build_cmd(fault_def, params, op)` 拼 `"<script>"`，参数通过**环境变量**传递（`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_<KEY>`），不走 argv，免注入。
- `executor_run`：用 `fork/exec + pipe` 同步执行，`waitpid` 等待子进程结束；用于 cnf 故障的 inject / clean 路径——dcat 等待脚本执行完返回。
- `executor_run_raw`：用 `system()` 直接执行，stdout/stderr 继承父进程终端（不 pipe 捕获）。用于 cnf 故障的 query 路径——用户需要看到脚本的原始输出（表格/多行文本），而非 JSON 包装。返回脚本退出码（0=成功）。
- `executor_clear_env_params`：清除 fault 定义中声明的所有 `DCAT_PARAM_*` 环境变量（6 个 per-op required/optional 列表的并集），在 clean 循环遍历多条记录时调用以防止前一条记录的参数泄漏到后一条。
- `mock_fn`：测试时注入，捕获 `(cmd)` 不真正执行；测试通过 `getenv()` 读取环境变量断言。
- **仅服务 cnf 故障**：注入器故障不经过 executor，直接调函数指针（§7.5）。
- **不提供** `executor_spawn` / `executor_kill` / `timer_create` 超时：本期统一同步阻塞执行，dcat 不托管子进程 pid。需要长驻的故障由脚本自行 spawn 子进程 + 写 pidfile/sidecar，clean 时重跑脚本读取清理。

### 3.5 precheck.c
- `precheck(fault_def, op, params)`：执行 SPEC §4.2 预检 4 步；返回 `result_t`（成功 / 带错误码）。
- 本模块只做静态校验（uid 存在、op 合法、参数齐全、脚本可执行），**不做交互确认**（本期不实现 safety_confirm）。
- **参数校验边界**：precheck 按 op 取对应的 `*_required` 列表校验齐全且非空（**结构校验**：inject 查 `inject_required`、clean 查 `clean_required`；**query 不强制必填**，无参时脚本展示全部）；参数值合法性（如 `cores` 范围、`iface` 是否存在）由脚本自行校验（**语义校验**）。`declared_params_only(inject_req, inject_opt, clean_req, clean_opt, query_req, query_opt, params)` 接收 6 个 per-op required/optional 列表，校验用户参数是否全部在声明范围内，未声明的参数直接拒绝（报错退出码 3）。query 跳过必填校验；inject/clean 若 `*_required` 列表为空，则空参数允许通过。
- **不做并发拦截**：precheck 只做静态结构校验（uid / op / 参数齐全 / 脚本可执行）；同 uid 同资源重复注入的拦截由 dispatch 层 reinject 检测处理（§8.4），不在 precheck。
- 注入器故障的预检由 `injector_t->precheck` 函数指针实现（§7.3），不走本模块。

### 3.6 state.c
- 静态 `injection_record_t g_records[DCAT_MAX_RECORDS]` + `pthread_mutex_t`。
- `state_add(uid, params)` → 分配 `record_id`，写 `params` / `started_at` / `active=1`。**仅 `inject,clean,query` 故障调用**（cnf 与注入器均如此）。
- `state_find_by_params(uid, params)`：按 uid + 用户提供的参数匹配活跃记录（用户提供的参数须与 inject 时参数一致才匹配）。供 `clean` 使用。
- `state_find(uid)` / `state_find_by_id(id)` / `state_list()`。
- `state_mark_inactive(id)`：clean 后置 `active=0`。
- `state_snapshot_by_uid(uid, out[], cap)`：在锁内拷贝该 uid 的活跃记录快照（避免 reinject 检测与 --force 逐条 clean 在 clean 循环中回调 state 造成重入死锁），供 §8 使用。
- 持久化：变更后写 `~/.demoncat/state.json`（cJSON 序列化），启动时加载（恢复 `record_id` 计数与未清理记录，含 params）。
- **不实现**自动清理线程 / `state_lazy_clean` / `expires_at` / `bg_pid`（本期不实现超时自动恢复；clean 一律由用户手动调用；统一同步执行无 dcat 托管 pid）。
- **不区分故障来源**：cnf 故障与注入器故障共用同一 state 表。

### 3.7 config.c
INI 解析 `demoncat.conf`：
- `[demoncat]` 段：`state_file` / `log_level`。
- `[fault.<uid>]` 段：载入 `fault_def_t`（含 6 个 per-op required/optional 字段，不含 `safety` / `timeout`）。
- 固定路径见 SPEC §7；`--config` 覆盖。
- **项目根推导**：`derive_project_root(cfgpath)` 从配置路径反推项目根（`<root>/config/demoncat.conf` → `<root>`）。
- **脚本路径解析**：`resolve_script(root, val, dst, cap)` 对相对脚本路径 prepend 项目根，使其变为绝对路径，让 dcat 可从任意 CWD 运行。已绝对（`/` 开头）或 home 相对（`~` 开头）的路径保持原样。当 root 为 `.`（测试用相对配置路径加载）时也保持原样，走 CWD 解析。
- 输出 `config_t { state_file, log_level, fault_def_t faults[], int fault_count }`。

### 3.8 output.c
- `result_t *result_ok(op, uid, cJSON *data)` / `result_err(op, uid, code, msg)`。
- `output_print(result_t*)`：JSON 打印到 stdout，`result_free` 释放。
- 统一 schema 见 SPEC §6。`inject`-only 的 `result_ok` 不写 `record_id` 字段。
- cnf 故障与注入器故障共用同一输出 schema。

### 3.9 dispatch.c
按 op 路由，串联各模块（见 §5 关键流程）。`inject` 分支区分 `inject`-only 与可恢复；`clean` / `query` 对 `inject`-only 故障在 precheck 阶段拒绝（退出码 3）。`serve` 子命令由 §3.12 serve.c 独立处理，不经 dispatch_route。

**按故障来源分流**（§1.4 三层）：
- **cnf 故障**（`registry_find` 命中 `fault_def_t`）：inject/clean 走 `executor_run`，query 走 `executor_run_raw`。
- **注入器故障**（`injector_find` 命中 `injector_t`）：直接调 `inj->inject/clean/query` 函数指针，不 fork、不设环境变量。
- **动态插件**（`plugin_find` 命中 `dcat_plugin_t`）：`plugin_dispatch` 调 `p->inject/clean/query` 函数指针，自带 precheck（§3.13）。

- `dispatch_clean_record(rec)`：传记录存储的 inject 参数给脚本 `DCAT_OP=clean` 或调 `inj->clean(params)`。由 `dispatch_clean` 调用，按用户参数匹配活跃记录，逐条执行 clean。**无 background→executor_kill 分支**（本期统一同步执行，clean 始终由脚本/注入器自行清理其子进程与系统资源）。
- **Reinject 默认拒绝**（§8）：inject 路径在调脚本前先做 `reinject_find_overlap` 资源重叠检测；同资源重叠且无 `--force` → 退出码 5 拒绝；`--force` → 逐条 clean 旧记录后重新 inject。`dispatch_route_force(uid, op, params, force)` 承载 force 参数，`dispatch_route(...)` 退化为 `force=0` 的 wrapper 保后向兼容。

### 3.10 help.c
- 子命令感知 `--help`：`dcat --help` 输出全局用法；`dcat inject --help` 列出所有支持 inject 的故障及参数；`dcat inject <uid> --help` 输出该故障的参数详情。
- 全局帮助列 `--config <path>` / `--plugins <dir>` 全局选项；inject/clean/query 子命令帮助尾部提示动态插件故障参数见 `dcat list`。
- `clean --all` / 无参 clean 说明纳入全局用法。

### 3.11 reinject.c
- `reinject_find_overlap(fault_def, params, ids[], cap)`：对同 uid 的活动记录做资源重叠检测（§8.4）。资源键 = `fault_def->clean_required` 各参数值；`cores` 走集合交集，其余走精确串等，多参键取各参精确 AND。
- `cores_parse(spec, bits)` / `cores_intersect(a, b)`：核集解析（`"0,1"` / `"0-3"` / 混合格式）+ 位 AND 交集（§8.5）。
- 被 dispatch.c inject 分支调用（§3.9）：同资源 overlap 且无 `--force` → 退出码 5；`--force` → 逐条 clean 旧记录后 inject。
- **仅 CNF 路径接入**；插件 / legacy injector 路径 deferred（§8.10）；inject-only 无 state 天然免检。

### 3.12 serve.c
- `dcat serve` HTTP 控制平面：内置静态前端（`src/web/`）+ `/api/*` 端点。
- **端点**：`/api/state`（活跃记录，从磁盘 reload state.json）、`/api/history`（历史）、`/api/catalog`（故障目录）、`/api/inject` + `/api/clean`（需 `--allow-write`）。
- **参数**：`--port`（默认 8080）、`--bind`（默认 `0.0.0.0`）、`--webroot`（覆盖内置前端目录）、`--allow-write`（默认只读）。
- **安全**：`realpath()` 路径穿越防护 + `%2e` URL 编码检测 + `--port` CLI 校验。
- 不经 `dispatch_route`；main.c 检测到 `op=serve` 时直接调用 serve 模块。

### 3.13 plugin_manager.c
- `plugin_load_dir(dir)`：扫描 `*.so`，`dlopen` + ABI 版本校验（`abi_version == DCAT_PLUGIN_ABI_VERSION`）+ `init()` + 注册到 `g_plugins[]`。
- `plugin_find(uid)` / `plugin_list()` / `plugin_count()` / `plugin_fini()`。
- 插件接口 `dcat_plugin_t`（`src/plugins/plugin.h`）：ABI 版本 + 元数据（uid/module/supported_ops/per-op 参数声明）+ `init`/`fini` 生命周期 + `precheck`/`inject`/`clean`/`query` 函数指针。
- **默认插件目录** `<root>/plugins`（`--plugins <dir>` 可覆盖）。
- 完整设计见 [Dynamic_Plugin_Implement.md](Dynamic_Plugin_Implement.md)。

---

## 4. 故障详设（按模块分组）

> 按模块分组详设，共享机制集中描述，差异以表呈现。
> 所有故障统一同步阻塞执行。需要长驻的故障（CPU 过载、端口占用、僵尸生成、磁盘写压等）由脚本自行 spawn 子进程 + 写 pidfile/sidecar 后立即返回；clean 重跑脚本读取 pidfile 清理。
> 本期所有故障均走 cnf+脚本路径；注入器路径（§7）留位待启用。

### 4.1 网络模块（network）

#### 4.1.1 共享机制

| 机制 | 命令 | 适用故障 | clean 策略 |
|---|---|---|---|
| `tc netem` | `tc qdisc add/replace dev <iface> root netem ...` | delay / loss / reorder / jitter | `tc qdisc del dev <iface> root`（幂等） |
| `tc tbf` | `tc qdisc add dev <iface> root tbf rate <rate>...` | bw_limit | `tc qdisc del` |
| `ip link` | `ip link set dev <iface> down/up` | down / link_flap | `ip link set up` |
| `tc` | `tc qdisc add/dev … tbf` | degrade | `tc qdisc del dev … root` |
| `iptables` | `iptables -I/-D INPUT/OUTPUT -p tcp --dport <port> -j DROP` | tcp_loss | `iptables -D` 删规则 |
| socket 占用 | python/socket 持有端口 | port_occupy | kill 持有进程（脚本读 pidfile） |
| systemctl | `systemctl stop/start <service>` | service_stop | `systemctl start` |

clean = dcat 重跑脚本 `DCAT_OP=clean`（传记录存储的 inject 参数）。脚本据此删 qdisc / 删 iptables 规则 / 起服务 / 读 pidfile kill 子进程。

#### 4.1.2 故障差异表

| UID | 必填参数 | 可选参数 | clean 机制 | 备注 |
|---|---|---|---|---|
| `rNET_delay` * | iface,delay_ms | — | `tc qdisc del` | 示例 |
| `rNET_loss` | iface,loss_pct | — | `tc qdisc del` | `netem loss random <pct>%` |
| `rNET_reorder` | iface,reorder_pct | — | `tc qdisc del` | `netem reorder <pct>%` |
| `rNET_down` | iface | — | `ip link set up` | clean 后保证 up |
| `rNET_degrade` | iface | speed_mbps(默认10) | `tc qdisc add … tbf rate` | speed_mbps 可选默认 10 |
| `rNET_port_occupy` | port | protocol(默认tcp) | kill 持有进程 | 脚本 spawn socket holder + pidfile，立即返回 |
| `rNET_service_stop` | service | — | `systemctl start <service>` | — |
| `rNET_link_flap` | iface | cycle_sec(默认2),count(默认10) | kill 闪断循环进程 + `ip link set up` | 脚本 spawn 循环进程 + pidfile，按 count 自结束 |
| `rNET_bw_limit` | iface,rate_kbps | — | `tc qdisc del` | `tbf rate <rate>kbps` |
| `rNET_jitter` | iface,delay_ms,jitter_ms | — | `tc qdisc del` | `netem delay <base> <jitter>` |
| `rNET_tcp_loss` | port | direction(默认both) | `iptables -D ...` | L4 丢包；direction ∈ {in,out,both} |

#### 4.1.3 错误处理

- `tc` / `iptables` / `systemctl` 不存在或无权限时，脚本非 0 退出，stderr 报错，dcat 纳入 `error.message`，退出码 1。
- `iface` 不存在时 `tc qdisc add` 报错，脚本退出非 0。
- 长驻型故障（port_occupy / link_flap）脚本 clean 时读 pidfile kill 子进程后清理。

### 4.2 进程模块（process）

#### 4.2.1 共享机制

| 机制 | 命令/工具 | 适用故障 | 备注 |
|---|---|---|---|
| `kill` 信号 | `kill -9/-STOP/-CONT <pid>` | exit / hang | exit 不可逆；hang 可逆 |
| 僵尸生成 | fork 子进程 + 子 exit + 父不 wait | zstate | count 个僵尸 |

#### 4.2.2 故障差异表

| UID | supported_ops | 必填 | 可选 | clean 机制 | 备注 |
|---|---|---|---|---|---|
| `rPROC_exit` | **inject** | pid | — | **N/A** | `kill -9 <pid>`，不可恢复；**唯一 inject-only 故障** |
| `rPROC_hang` | inject,clean,query | pid | — | `kill -CONT <pid>` | `kill -STOP`；clean 重跑脚本发 CONT |
| `rPROC_zstate` | inject,clean,query | pid | — | kill 父进程（子被 init reap） | kill 目标进程 → 僵尸；clean 杀父进程回收 |

#### 4.2.3 rPROC_exit 的 inject-only 语义

- `supported_ops = inject`：目录中仅声明 `inject_required`，不声明 `inject_optional` / `clean_*` / `query_*` 字段。
- dispatch 走 inject-only 分支：执行 → `output_ok(message)`，**不写 state**。
- `dcat clean rPROC_exit` / `dcat query rPROC_exit` 在 precheck 阶段拒绝（op 不在 supported_ops，退出码 3）。

### 4.3 CPU 模块（cpu）

#### 4.3.1 共享机制

| UID | 机制 | 命令 | clean |
|---|---|---|---|
| `rCPU_overload` * | 多核 burn（纯用户态） | `perl -e '1 while 1'` 多实例 + taskset | kill 进程组（脚本读 pidfile） |
| `rCPU_core_offline` | sysfs 离线 | `echo 0 > /sys/devices/system/cpu/cpu<N>/online` | `echo 1 > ...` |

#### 4.3.2 rCPU_core_offline 详设

- **必填**：`cores`（格式 `"0,2,4"` 或 `"0-3"`，脚本解析）。
- 脚本 `for n in cores; do echo 0 > .../cpu<N>/online; done`，执行完返回。clean = 重跑脚本 `DCAT_OP=clean`，对所有 cores `echo 1`。
- **限制**：cpu0 通常不可离线（内核 CONFIG_BOOTPARAM_HOTPLUG_CPU0），脚本须跳过并 stderr 提示。

### 4.4 存储模块（storage）

#### 4.4.1 rDISK_write_overload

- **必填**：`device`（块设备路径，如 `/dev/sda` 或挂载点 `/data`）。
- **可选**：`workers`（默认 4）。
- 脚本 spawn N 个 `dd if=/dev/zero of=<device>/dcat.stress bs=1M` 或 `fio --rw=write --numjobs=N`，写 pidfile 后立即返回。
- **clean** = 重跑脚本 `DCAT_OP=clean`：读 pidfile kill 进程组，dd/fio 自然终止；可选 `rm -f <device>/dcat.stress`。
- **预检**：`device` 存在且可写；`fio` 或 `dd` 可执行。

### 4.5 NPU 模块（npu）

#### 4.5.1 共享机制

所有 NPU 故障通过 `hccn_tool -i <chip> <op>` 操作 RoCE 网卡。脚本共享 `src/scripts/npu/_common.sh`（`npu_check_env` / `npu_validate_chip` / `sidecar_save/load/clear`），每脚本内联 `fault_present` 函数实现 query-then-clean 幂等。

| clean 策略 | 适用 UID | 机制 |
|---|---|---|
| 反向操作 | arp_poison, route_add, iprule_add, iproute_add | del 加的 / add 删的 |
| sidecar 回放 | ip_change, gw_change, netdetect_change, arp_del, route_del, iprule_del, iproute_del, mtu, dscp_tc, roce_port | inject 前 -g 存原值；clean 回放 |
| 设回 max | bw_limit | clean = -shaping -s bw_limit 100000 |
| -cfg recovery | link_down | hccn_tool 内置恢复 |

#### 4.5.2 故障差异表

（见 SPEC §3.3 完整目录表）

#### 4.5.3 约束

- 所有 NPU 故障同步执行（hccn_tool 是快命令，执行完返回）。
- 所有故障 `inject_required` / `clean_required` 含 `chip`（hccn_tool 的 `-i <0~7>`）；query 参数 `chip` 声明在 `query_optional`（query 不强制必填，无参时脚本查全部芯片）。
- 真实环境冒烟仅华为 Atlas 物理机可做（CI mock-only，与网络类一致）。

---

## 5. 关键流程

### 5.1 inject（含 inject-only 分支 + 注入器回退）

```
parse → registry_find(uid)
  ├─ cnf 命中 fault_def:
  │     → precheck(op∈supported_ops, inject_required 齐全, 脚本可执行)
  │     → dispatch:
  │        ├─ inject-only:    executor_run(script, DCAT_OP=inject) → output_ok (无 state)
  │        └─ 可恢复:          executor_run(script, DCAT_OP=inject) → state_add(uid, params)
  │                          → output_ok(message, record_id)
  ├─ cnf 未命中 → injector_find(uid) (§7.4):
  │     ├─ 命中 injector_t:
  │     │     → inj->precheck(op, params)
  │     │     → dispatch:
  │     │        ├─ inject-only: inj->inject(params) → output_ok (无 state)
  │     │        └─ 可恢复:       inj->inject(params) → state_add(uid, params)
  │     │                          → output_ok(message, record_id)
  │     └─ 未命中 → plugin_find(uid) (§3.13):
  │           ├─ 命中 dcat_plugin_t:
  │           │     → plugin_dispatch(p, op, params)
  │           │       (ABI 校验 + per-op precheck + p->inject(params))
  │           │     → state_add (if p->clean != NULL)
  │           └─ 未命中 → output_err(4, "not found")
```

> **Reinject 检测**（§8）：CNF 可恢复分支在 `executor_run(DCAT_OP=inject)` 之前先做 `reinject_find_overlap` 资源重叠检测——同资源 overlap 且无 `--force` → 退出码 5 拒绝；`--force` → 逐条 clean 旧记录（`executor_run` DCAT_OP=clean + `state_mark_inactive`）后再 inject。仅 CNF 路径接入；注入器路径 deferred（§8.10）；inject-only 无 state 天然免检。

### 5.2 clean

```
parse → registry_find(uid)
  ├─ cnf 命中 fault_def:
  │     → precheck(op=clean ∈ supported_ops, clean_required 齐全)  # inject-only 故障在此拒绝 (退出码 3)
  │     # 无 clean_required 的故障允许空参数; 有 clean_required 则缺参数被 precheck 拒绝
  │     → state_find_by_params(uid, params)       # 按用户参数匹配活跃记录
  │        ├─ 无记录 → output_err(1, "no active injection")
  │        └─ 记录(s) → 逐条 executor_run(script, DCAT_OP=clean, 传记录存储的 inject 参数)
  │                   # 某条失败时停止，剩余不清理；成功条目 mark inactive
  │        → state_mark_inactive(每条匹配记录) → output_ok
  └─ cnf 未命中 → injector_find(uid):
        ├─ 命中 injector_t:
        │     → inj->precheck(clean, params)
        │     → state_find_by_params(uid, params)
        │        ├─ 无记录 → output_err(1, "no active injection")
        │        └─ 记录(s) → inj->clean(params)
        │        → state_mark_inactive(每条匹配记录) → output_ok
        └─ 未命中 → output_err(4, "not found")
```

### 5.3 query / list

- **query（无 uid）**：`state_list` 遍历活跃记录，输出记录数组 JSON。不调用脚本/注入器。
- **query（有 uid）**：验证故障是否真的生效。用户参数传入（与 inject 参数独立）。
  - cnf 故障：`executor_run_raw_fault(f, "query", params)`，脚本 stdout 原样输出到终端。
  - 注入器故障：`inj->query(params)`，函数返回的 result_t 中携带证据文本。
  - dcat 打印 `---` 分隔符后输出 JSON `{"confirmed":true/false}`。inject-only 故障在 precheck 拒绝（退出码 3）。
  - **query 不强制必填参数**：precheck 对 query 不做必填校验（与 inject/clean 不同）。无参时脚本自行展示全部（如全部核/全部网卡），有参则按参过滤；query 参数声明在 `query_optional`。`query_required` 已弃用（解析兼容保留，留空）。
- **list**：`registry_list()`，输出 cnf fault 目录文本表格（含 supported_ops / 6 个 per-op required/optional 字段 / desc）。注入器故障本期不纳入 list 输出。

---

## 6. 脚本契约（实现约定）

### 6.1 通用约定

- 环境变量传参：`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_<KEY>`（KEY 大写、非字母数字→`_`）。
- **参数声明按操作分字段**：`inject_required` / `inject_optional` / `clean_required` / `clean_optional` / `query_required` / `query_optional`（conf 中空字段可省略）。dcat 按 `DCAT_OP` 取对应 required/optional 列表做校验并下发 `DCAT_PARAM_*` 环境变量。
- 退出码 `0`=成功；非 `0`=失败。
- stdout 成功文本 → `data.message`；stderr → `error.message`。
- 可选参数未提供时，对应 `DCAT_PARAM_<KEY>` 环境变量不设置；脚本须自行处理默认值（目录表中 `(默认X)` 标注）。
- **同步阻塞**：dcat `fork/exec + waitpid` 等待脚本执行完返回。脚本**不应前台驻留阻塞 dcat**；需要长驻的故障由脚本自行 spawn 子进程 + 写 pidfile/sidecar（如 `/tmp/dcat-<uid>.pid`）后立即返回。

### 6.2 可恢复故障脚本

- inject 脚本执行完即返回。需要长驻的故障（CPU 过载、端口占用、僵尸生成、磁盘写压等）自行 spawn 子进程 + 写 pidfile/sidecar 后立即返回。
- clean 时 dcat 按用户参数匹配活跃记录（precheck 按 op 校验 `clean_required` 齐全；无 `clean_required` 的故障允许空参数，有 `clean_required` 则缺参数被拒绝退出码 3），传记录存储的 inject 参数给脚本 `DCAT_OP=clean`，逐条执行。某条失败时停止，剩余不清理。脚本据此清理资源（删 qdisc / 删 iptables 规则 / 起服务 / 读 pidfile kill 子进程）。脚本退出码非 0 时 dcat 报错且**不 mark inactive**（故障可能仍在系统上）。
- **无 uid 的 query** 由 dcat 状态回答，脚本无需实现该路径；**有 uid 的 query** 由 dcat 调脚本 `DCAT_OP=query` 分支验证（参数为用户当前输入，非 inject 时参数）。脚本 stdout 原样输出 + `---` + JSON。

### 6.3 inject-only 故障脚本

- 仅实现 `DCAT_OP=inject` 分支；不期望 `DCAT_OP=clean`。
- 目录中不声明 `inject_optional`（inject-only 故障无可选参数）；`clean_*` / `query_*` 字段也省略。
- inject 完成即终结，不写 state。

### 6.4 默认值约定

- 目录表中 `(默认X)` 表示该可选参数（`inject_optional` / `clean_optional` / `query_optional` 中声明的）缺省时的脚本内部默认值。
- 脚本须在 `DCAT_PARAM_<KEY>` 未设置时使用该默认值，**不应依赖 dcat 注入默认值到环境变量**（dcat 不解析默认）。

---

## 7. 注入器设计实现

### 7.1 设计动机与定位

编译注入器（injector）是 cnf+脚本路径的**进程内高级扩展点**，用于少数脚本难以胜任的故障：

- **精确定时**：需要微秒级定时控制的故障（脚本启动开销过大）。
- **二进制协议**：需要与内核/硬件直接交互的故障（ioctl、特殊系统调用、内存映射）。
- **进程内状态**：需要在 dcat 进程内维护跨调用状态的故障（脚本每次 fork 丢失状态）。

> 绝大多数故障应走 cnf+脚本路径（§1.2 + §6）。注入器仅用于脚本无法实现的场景。本期 `builtin_injectors[]` 为空数组，仅头文件与接口设计留位；待出现脚本无法实现的需求时启用。

### 7.2 injector_t 接口定义

```c
/* src/injectors/injector.h */

#ifndef DCAT_INJECTOR_H
#define DCAT_INJECTOR_H

#include "core/types.h"

/* 注入器接口：uid + 4 个函数指针
 * 函数指针契约与脚本契约（§6）对齐，但直接在 dcat 进程内执行，
 * 不走 fork/exec、不走环境变量，参数通过 params_t 结构体传入。
 */
typedef struct injector_t {
    const char *uid;        /* 故障唯一标识，与 cnf uid 命名空间一致 */

    /* 注入故障；返回 result_t(含 data.message)。
     * inject-only 注入器：clean/query 指针可为 NULL。 */
    result_t *(*inject)(const params_t *params);

    /* 清除故障；传记录存储的 inject 参数。返回 result_t。
     * inject-only 注入器此指针为 NULL。 */
    result_t *(*clean)(const params_t *params);

    /* 查询故障是否生效；返回 result_t(含 data.confirmed + 证据文本)。
     * inject-only 注入器此指针为 NULL。 */
    result_t *(*query)(const params_t *params);

    /* 预检：op∈{inject,clean,query}。执行 SPEC §4.2 预检 4 步的等价校验。
     * inject-only 注入器对 clean/query 请求返回退出码 3。
     * 返回 result_t（成功 / 带错误码）。 */
    result_t *(*precheck)(const char *op, const params_t *params);
} injector_t;

/* 编译期静态注册表（§7.4） */
extern const injector_t *const builtin_injectors[];
extern const int builtin_injector_count;

/* 按 uid 线性扫描 builtin_injectors[]；命中返回指针，未命中返回 NULL */
const injector_t *injector_find(const char *uid);

#endif /* DCAT_INJECTOR_H */
```

### 7.3 接口契约

注入器函数指针的契约与脚本契约（§6）对齐，但**直接在 dcat 进程内执行**，不走 fork/exec、不走环境变量：

| 函数指针 | 等价脚本路径 | 返回 `result_t` 语义 |
|---|---|---|
| `inject` | `DCAT_OP=inject` | 成功：`result_ok(op=inject, data={message})`；失败：`result_err(code, msg)` |
| `clean` | `DCAT_OP=clean` | 成功：`result_ok(op=clean, data={message})`；失败：`result_err` |
| `query` | `DCAT_OP=query` | 成功：`result_ok(op=query, data={confirmed, <证据文本>})`；失败：`result_err` |
| `precheck` | SPEC §4.2 预检 4 步 | 成功：`result_ok`；失败：`result_err(code, msg)` |

约定：

- **参数传递**：通过 `params_t *` 结构体指针，不设环境变量。注入器实现自行从 `params_t` 取参数（`params_find(params, "iface")`）。
- **输出**：注入器直接构造 `result_t`（调 `result_ok` / `result_err`，§3.8），不走 stdout pipe。query 的证据文本放入 `result_t` 的 `data` 字段（JSON 字符串），由 dcat 统一打印 `---` 分隔符后输出。
- **inject-only 注入器**：`clean` / `query` 指针为 NULL；`precheck` 对 clean/query 请求返回退出码 3。
- **state**：可恢复注入器（`clean` 非 NULL）的 inject 成功后由 dispatch 写 state（与 cnf 故障一致，§3.6）；inject-only 注入器不写 state。
- **超时**：注入器在 dcat 进程内执行，无 fork 超时机制；若需超时保护，注入器实现自行用 `timer_create` 或 alarm。

### 7.4 注册与查找

**注册**：编译期静态数组，在 `src/injectors/injectors.c` 中定义：

```c
/* src/injectors/injectors.c */

#include "injector.h"

/* 本期为空数组；后续按需添加编译注入器实现。
 * 示例（待启用时取消注释）：
 *   extern const injector_t rMEM_ecc_injector;
 *   const injector_t *const builtin_injectors[] = {
 *       &rMEM_ecc_injector,
 *   };
 */
const injector_t *const builtin_injectors[] = {
    /* &rMEM_ecc_injector, */
};
const int builtin_injector_count =
    sizeof(builtin_injectors) / sizeof(builtin_injectors[0]);

const injector_t *injector_find(const char *uid) {
    for (int i = 0; i < builtin_injector_count; i++) {
        if (strcmp(builtin_injectors[i]->uid, uid) == 0) {
            return builtin_injectors[i];
        }
    }
    return NULL;
}
```

**查找顺序**（§3.3 registry）：`registry_find(uid)` 先查 cnf `fault_def` 表；未命中再调 `injector_find(uid)` 线性扫描 `builtin_injectors[]`。两处都未命中 → 退出码 4。

> cnf 优先保证数据驱动扩展的免重编译特性不被破坏；注入器仅作回退，用于无法脚本化的场景。

### 7.5 dispatch 路由

dispatch 按 fault 来源分流（§3.9 / §5）：

```
dispatch_route(uid, op, params):
  fault = registry_find(uid)            # 第1层: cnf 优先
  if fault != NULL:
      → precheck(fault, op, params)     # §3.5
      → executor_run / executor_run_raw # §3.4 调脚本 (现有 cnf 流程)
  else:
      inj = injector_find(uid)          # 第2层: 回退注入器 (§7.4)
      if inj != NULL:
          → inj->precheck(op, params)   # 注入器自带预检
          → inj->op(params)            # 直接函数调用, 不 fork/不设 env
      else:
          p = plugin_find(uid)          # 第3层: 回退动态插件 (§3.13)
          if p != NULL:
              → plugin_dispatch(p, op, params)  # ABI 校验 + per-op precheck + 函数指针
          else:
              → output_err(4, "not found")
```

- cnf 故障：inject/clean 走 `executor_run`，query 走 `executor_run_raw`（§3.4）。
- 注入器故障：直接调 `inj->inject/clean/query` 函数指针，不 fork、不设环境变量。
- 注入器故障的 state / precheck / inject-only 语义与 cnf 故障一致（§4.2 / §5.1）：可恢复注入器仍写 state、query 无 uid 仍由 state 回答。

### 7.6 与 cnf+脚本路径的关系

| 维度 | cnf+脚本 | 编译注入器 |
|---|---|---|
| 实现方式 | 外部 `.sh` 脚本 | C 函数，编译进二进制 |
| 新增成本 | 加脚本+cnf 段，**免重编译** | 改 `injectors.c`，**需重编译** |
| 执行方式 | `fork/exec` 子进程 | 进程内直接调用 |
| 参数传递 | 环境变量 `DCAT_PARAM_*` | `params_t` 结构体指针 |
| 输出 | stdout→pipe 捕获 / `run_raw` 直通 | 直接构造 `result_t` |
| 超时 | 无（同步 waitpid） | 注入器自行实现 |
| 适用场景 | 大多数故障（tc/iptables/kill 等） | 精确定时/二进制协议/进程内状态 |
| state 记录 | 可恢复故障写 state | 同左 |
| list 输出 | 纳入 | 本期不纳入（注入器本期为空） |
| 优先级 | `registry_find` 优先 | `injector_find` 回退 |

> **YAGNI**：本期 `builtin_injectors[]` 为空数组，仅头文件 `injector.h` 与 `injectors.c` 留位。所有故障均走 cnf+脚本路径。注入器接口设计完成，待后续有脚本无法实现的需求时启用。

---

## 8. Reinject 默认拒绝与原子替换（--force）

> 对同一资源的重复注入从"隐式并集/覆盖"改为"默认拒绝 + `--force` 原子替换"。TDD 完成，由 `tests/test_reinject.c` 覆盖（见 Test_Report.md / §11）。

### 8.1 目标与动机

对同一资源的重复注入，由旧的"隐式并集/覆盖"改为**默认拒绝**；只有显式带上 `--force` 时才原子替换旧记录。

动机场景：`rCPU_overload cores=0,1` 已注入，再 `cores=0-8`（核集相交）应**拒绝**，而非旧加法并集共存；需显式 `--force` 才原子替换为 `0-8`。

### 8.2 适用范围（v1）

- **CNF 路径**（config 驱动的 58 条故障，`registry_find` 命中）：适用。
- **inject-only 故障**（`supported_ops="inject"`，不写 state）：天然 0 overlap → 自然免检，无需特殊处理。
- **插件路径**（`plugin_dispatch`）、**legacy injector 路径**（`injector_find`）：deferred，本期不接入（§8.10）。

### 8.3 资源键

- **资源键 = `f->clean_required` 各参数值**（已验 60 条 cnf 故障的 `clean_required` 均为纯资源标识：cores / device / iface / port / service / pid / chip*），零新增 config 字段。
- **`cores` 硬编码为集合语义参数**（唯一集合参数）：走集合交集；其余参数（device / iface / port / service / pid / chip / dev / ip / …）走精确串等；多参键（如 NPU `chip,dev,ip`）取各参精确 AND。
- 备选方案"新增 conf `resource_key` 字段"按 YAGNI 舍弃（`clean_required` 已等价）。

### 8.4 overlap 检测算法

对每个同 uid 的活动记录 R（经 `state_snapshot_by_uid` 在锁内拷贝快照，避免回调重入死锁，§3.6）：

```
resource_overlaps(new, R, clean_required):
  for tok in clean_required.split(','):
    nv = params_find(new, tok); rv = params_find(R, tok)
    if !nv or !rv: continue              # 缺参 → 该 param 不贡献 overlap（留给 precheck 报 missing）
    if tok == "cores":
      nb, rb = cores_parse(nv), cores_parse(rv)   # 任一解析失败 → continue（不阻塞，留给脚本报错）
      if !cores_intersect(nb, rb): return false   # 核集不相交 → 非同资源
    else:
      if strcmp(nv, rv) != 0: return false        # 标量不等 → 非同资源
  return true                                     # 全部资源参 overlap → 同资源
```

- 任一 R overlap → **REJECT**（收集所有 overlap record id）。
- `--force` → 逐个 clean 旧 overlap 记录（复用 clean 路径：`executor_run` DCAT_OP=clean + `state_mark_inactive`，§5.2），全成功后再 inject。
- clean 失败 → 中止注入（返回 err），已清记录丢失；inject 失败则旧记录已清（操作者可重注）。
- **非真原子**（两步脚本：先 clean 后 inject），存在窗口（§8.10）。

### 8.5 cores 解析器

- spec：`"0,1" | "0-3" | "0,1,4-6" | "0"` → 位图 `unsigned char bits[16]`（128 bit，核 0–127）。
- `cores_parse(spec, bits)`：split `,`，token 含 `-` → lo-hi 区间置位，否则单核置位；越界/非法 → 返回 -1。
- `cores_intersect(a, b)`：位 AND，任一位置 1 → 1。

### 8.6 CLI（--force）

- `parsed_cmd_t` 新增 `int force;`（§3.2）。
- `cli.c`：`--force` 当布尔 flag（同 `--help`，裸出现 → `force=1`）；`--force=x` → 报错 "`--force` does not take a value"。
- `--force` 仅 inject 路径生效；clean / query / list 出现 → 忽略（无害，兼容历史脚本）。

### 8.7 dispatch 路由改动

`dispatch_route_force(uid, op, params, force)` 承载 force 参数（§3.9）；`dispatch_route(...)` 退化为 `force=0` 的 wrapper，保后向兼容，现有调用零改动。CNF inject 分支：

```
if (strcmp(op, "inject") == 0) {
    int ids[DCAT_MAX_RECORDS];
    int n = reinject_find_overlap(f, params, ids, DCAT_MAX_RECORDS);   // §8.4
    if (n > 0 && !force)
        return result_err("inject", f->uid, 5,
                           "resource already injected; use --force to replace");
    if (n > 0 && force)
        for each id: dispatch clean (executor_run DCAT_OP=clean) + state_mark_inactive;
                   失败返回 err（已清记录丢失）
    return executor_run(script, DCAT_OP=inject) → state_add(uid, params) → output_ok(record_id);
}
```

- **退出码 5 = reinject conflict**。
- inject-only 分支无 state → `reinject_find_overlap` 恒 0 → 不拒绝（§8.2）。

### 8.8 向后兼容（BREAKING）

- **CPU `cores` 加法并集（PR#15）→ 默认拒绝**：有意 breaking（用户的动机场景）。
  - `0,1` 已注入 → 再 `0,1`（同）：旧 idempotent-ok，现 **REJECT**。
  - `0,1` 已注入 → `0-8`（重叠）：旧并集共存，现 **REJECT**。
- 网络 / 进程 / 存储：本就同资源不可并存（tc qdisc / ipset / 单 pid），只是把隐式打架显式化为 reject，基本非 breaking。
- 迁移：重注入改加 `--force`；不同资源（不重叠核 / 不同 iface）仍并发 OK。

### 8.9 测试计划（TDD，`tests/test_reinject.c`，先全红）

1. `cores_parse`：`"0,1"`→{0,1}；`"0-3"`→{0,1,2,3}；`"0,1,4-6"`→{0,1,4,5,6}；`"0"`→{0}；非法→-1。
2. `cores_intersect`：{0,1}∩{0,2}={0}；{0,1}∩{2,3}=∅；{0,1}∩{0-8}={0,1}。
3. overlap 精确：mock inject `rCPU_overload cores=0,1` → 活动；再 `0,1`（force=0）→ code 5 REJECT；`2,3` → OK。
4. overlap 集合：`0,1` 活动；再 `0-8`（force=0）→ REJECT；`4,5` → OK。
5. `--force` 替换：`0,1` 活动；`--force 0-8` → 旧 `0,1` 被清，`0-8` 活动（state 唯一，cores=0-8）。
6. 网络标量：`rNET_delay iface=eth0 delay_ms=100` 活动；再 `iface=eth0 delay_ms=200`（force=0）→ REJECT；`iface=eth1` → OK；`--force iface=eth0 delay_ms=200` → 替换（state 唯一 iface=eth0）。
7. 多参键（NPU，mock state 不跑脚本）：`rNPU_ip_change chip=0 dev=eth0 ip=1.1.1.1` 活动；再 `ip=2.2.2.2` → OK（不同资源）；再 `ip=1.1.1.1` → REJECT；`--force ip=1.1.1.1` → 替换。
8. inject-only 免检：`rPROC_exit inject` → 无 state；再 inject → OK（0 overlap，不 reject）。
9. CLI：`inject rCPU_overload --cores=0,1 --force` → `pc.force==1`；无 `--force` → 0；`--force=x` → error；clean 带 `--force` → 忽略不报错。

连带行为变更：

- `test_smoke_cpu.c` test1（同规格重注入 idempotent-ok）→ 改断言 REJECT（无 --force）/ OK replace（--force）。
- `test_smoke_cpu.c` test2（`0-1`→`0` 重叠 idempotent-ok）→ 改断言 REJECT（无 --force）。

### 8.10 deferred 与约束

- 插件 / legacy injector 路径未接入 reinject（CNF 路径已覆盖 33 故障）。
- `clean_required` 为空且写 state 的故障（本期无此 fault）：保守判 overlap（任意活动 = 冲突）。
- 真原子性：两步脚本（先 clean 后 inject）存在窗口，未做事务回滚。

---

## 9. 目录结构

```
DemonCAT/
├── CMakeLists.txt              # C11, -Wall -Wextra -Werror, cjson + pthread + dlopen
├── LICENSE                     # Apache-2.0
├── .gitattributes             # 强制 LF 行尾
├── .gitignore
├── README.md
├── SPEC.md                     # 技术规格
├── User_Manual.md              # 用户手册（58 条故障 × 7 字段）
├── Release_Notes.md           # 版本发布记录
├── config/
│   └── demoncat.conf           # 故障目录配置（58 条故障声明）
├── docs/
│   ├── DESIGN.md               # 本文件：架构设计
│   ├── DemonCAT_Error_List.md  # 故障目录（58 条）
│   ├── Dynamic_Plugin_Implement.md # 动态插件开发指南
│   ├── Manual_Test_Reference.md # 手动测试参考
│   └── Test_Report.md          # 测试报告
├── scripts/
│   └── install_deps.sh         # 一键安装依赖（幂等）
├── src/
│   ├── main.c                  # 入口：读配置→解析→调度→输出
│   ├── core/
│   │   ├── cli.{c,h}           # 命令解析器
│   │   ├── config.{c,h}        # INI 解析 + 项目根推导
│   │   ├── registry.{c,h}     # fault_def 表 + 查找
│   │   ├── executor.{c,h}     # fork/exec 同步执行
│   │   ├── precheck.{c,h}     # SPEC §4.2 预检 4 步
│   │   ├── state.{c,h}        # 注入记录 + cJSON 持久化 + pthread
│   │   ├── dispatch.{c,h}     # 按 op 分发：3-tier（cnf→injector→plugin）
│   │   ├── output.{c,h}       # JSON 输出
│   │   ├── help.{c,h}         # 子命令感知 --help
│   │   ├── reinject.{c,h}     # 资源重叠检测 + --force 原子替换（§8）
│   │   ├── serve.{c,h}        # HTTP 控制平面（dcat serve）
│   │   └── types.{c,h}        # 公共类型: params_t/result_t/fault_def_t/injection_record_t
│   ├── injectors/
│   │   ├── injector.h         # 注入器接口 injector_t（§7.2）
│   │   └── injectors.c        # builtin_injectors[]（本期为空，§7.4）
│   ├── plugins/
│   │   ├── plugin.h           # 动态插件接口 dcat_plugin_t
│   │   ├── plugin_manager.{c,h} # dlopen 加载 + ABI 校验 + 查找
│   │   └── sample/
│   │       └── sample_plugin.c # 示例插件 rSAMPLE_test
│   ├── scripts/
│   │   ├── cpu/               # CPU 模块（2 条）
│   │   │   ├── cpu_overload.sh
│   │   │   └── cpu_core_offline.sh
│   │   ├── network/           # 网络模块（11 条）
│   │   │   ├── net_delay.sh
│   │   │   ├── net_loss.sh
│   │   │   ├── net_reorder.sh
│   │   │   ├── net_down.sh
│   │   │   ├── net_degrade.sh
│   │   │   ├── net_port_occupy.sh
│   │   │   ├── net_service_stop.sh
│   │   │   ├── net_link_flap.sh
│   │   │   ├── net_bw_limit.sh
│   │   │   ├── net_jitter.sh
│   │   │   └── net_tcp_loss.sh
│   │   ├── process/           # 进程模块（3 条）
│   │   │   ├── proc_exit.sh
│   │   │   ├── proc_hang.sh
│   │   │   └── proc_zstate.sh
│   │   ├── storage/           # 存储模块（1 条）
│   │   │   └── disk_write_overload.sh
│   │   └── npu/               # NPU 模块（16 条）+ _common.sh
│   │       ├── _common.sh
│   │       ├── link_down.sh
│   │       ├── ip_change.sh
│   │       ├── gw_change.sh
│   │       ├── netdetect_change.sh
│   │       ├── arp_poison.sh
│   │       ├── arp_del.sh
│   │       ├── route_add.sh
│   │       ├── route_del.sh
│   │       ├── iprule_add.sh
│   │       ├── iprule_del.sh
│   │       ├── iproute_add.sh
│   │       ├── iproute_del.sh
│   │       ├── bw_limit.sh
│   │       ├── mtu_mismatch.sh
│   │       ├── dscp_tc_change.sh
│   │       └── roce_port_change.sh
│   └── web/                   # Web 控制台静态前端（dcat serve 内置）
│       ├── index.html
│       ├── app.js
│       └── style.css
├── third_party/cjson/
│   ├── cJSON.c               # vendored JSON 库
│   └── cJSON.h
└── tests/
    ├── test.h                 # 测试框架宏
    ├── test_faults_common.h   # 通用 mock + 断言宏
    ├── test_types.c           # Tier 0: params_t helpers
    ├── test_output.c          # Tier 0: result_ok/err/print
    ├── test_config.c          # Tier 0: INI 解析
    ├── test_registry.c        # Tier 0: fault_def 查找
    ├── test_executor_mock.c   # Tier 0: mock 拦截
    ├── test_precheck.c        # Tier 0: per-op required 校验
    ├── test_state.c           # Tier 0: 记录 + 持久化 + 并发
    ├── test_injectors.c       # Tier 0: injector_t 接口
    ├── test_dispatch.c        # Tier 0: 3-tier 路由 + reinject
    ├── test_reinject.c        # Tier 0: 资源重叠检测 + --force
    ├── test_cli.c             # Tier 0: 子命令解析
    ├── test_faults.c          # Tier 0: 表驱动示例
    ├── test_help.c            # Tier 0: --help 系统
    ├── test_plugin_manager.c     # Tier 0b: dlopen + ABI
    ├── test_plugin_integration.c # Tier 0b: 插件集成
    ├── test_faults_cpu_storage.c # Tier 1: 2 条 CPU + 1 条存储
    ├── test_faults_network.c      # Tier 1: 11 条网络
    ├── test_faults_process.c      # Tier 1: 3 条进程
    ├── test_faults_npu.c          # Tier 1: 16 条 NPU
    ├── check_syntax.sh            # Tier 2: sh -n 语法检查
    ├── test_smoke_cpu.c          # Tier 3: CPU 真实执行
    ├── test_smoke_process.c      # Tier 3: 进程真实执行
    ├── test_smoke_storage.c      # Tier 3: 存储+端口真实执行
    ├── test_smoke_state_lost.c   # Tier 3: state 丢失 stateless clean
    ├── smoke_root.sh             # root 级自动化测试
    └── e2e/                      # E2E 测试框架
        ├── README.md
        ├── cases.csv             # 354 步骤 / 165 流程
        ├── gen_cases.py          # 用例生成
        └── run_e2e.py            # 执行框架
```

> `src/injectors/` 目录含 `injector.h`（接口定义，§7.2）与 `injectors.c`（注册表 + 查找，§7.4）。本期 `builtin_injectors[]` 为空数组。

---

## 10. 构建

- `CMakeLists.txt`：`set(CMAKE_C_STANDARD 11)`（gnu11 扩展开启，便于 `usleep`/`select`/`fork`），`_POSIX_C_SOURCE=200809L` 编译定义（确保 strict C11 可移植），静态链接，`find_package(Threads)`，把 `third_party/cjson/cJSON.c`、`src/injectors/injectors.c`、`src/plugins/plugin_manager.c` 与 `src/core/` 全部 12 模块（含 `serve.c` / `reinject.c` / `help.c`）编进 `DCAT_CORE`，`-Wall -Wextra -Werror`。`target_link_libraries(dcat PRIVATE cjson Threads::Threads ${CMAKE_DL_LIBS})`。
- 示例插件 `src/plugins/sample/sample_plugin.c` 编为 MODULE 库 → `plugins/libsample.so`（构建产物，`.gitignore` 忽略）。`test_plugin_integration` 通过 `add_dependencies` 依赖此目标。
- 目标 `dcat`；测试通过 `enable_testing()` + `add_test`，`ctest` 驱动。测试 `WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}` 以便 `config/demoncat.conf` 与 `src/scripts/*.sh` 解析。
- WSL 验证：`mkdir -p build && cd build && cmake .. && make -j8 && ctest --output-on-failure`。`WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}` 以便 `config/demoncat.conf` 与 `src/scripts/*.sh` 解析。

---

## 11. 测试设计

> 开发遵循 TDD：先写测试用例（定义期望命令串 + 环境变量 + 退出码 + JSON 输出），再实现功能代码使测试通过。测试用例是行为的权威定义（见 SPEC §9.1）。
> Reinject 默认拒绝与 --force 的测试设计见 §8.9（`tests/test_reinject.c`）。

### 11.1 mock_executor

`executor_set_mock(fn)`，`fn` 捕获 `(cmd)` 不真正 fork。测试通过 `getenv()` 读取环境变量断言 cnf 故障下发命令串与环境变量集合（`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_*`）。

> 注入器故障不经过 executor，mock_executor 不适用；注入器测试直接断言 `inj->op(params)` 返回的 `result_t`。

### 11.2 表驱动

以 `struct { input; fault_def; expect_cmd; expect_env; expect_record; expect_json; expect_exit; }` 数组驱动 inject / clean / query（cnf 故障）。

### 11.3 故障覆盖矩阵

| 故障 | inject | clean | query | mock 断言 | 真实冒烟 |
|---|:---:|:---:|:---:|---|---|
| rNET_loss / reorder / bw_limit / jitter | ✓ | ✓ | ✓ | 命令串含 `tc qdisc add ... netem/tbf`；env 含 iface/... | 可选（需 root + iface） |
| rNET_down | ✓ | ✓ | ✓ | 命令串含 `ip link set down` | 可选 |
| rNET_degrade | ✓ | ✓ | ✓ | 命令串含 `tc qdisc … tbf` | 可选 |
| rNET_port_occupy | ✓ | ✓ | ✓ | env 含 port/protocol；脚本 spawn holder + pidfile | 可选 |
| rNET_service_stop | ✓ | ✓ | ✓ | 命令含 `systemctl stop` | 可选 |
| rNET_link_flap | ✓ | ✓ | ✓ | 脚本 spawn 循环进程 + pidfile；按 count 自结束 | 可选 |
| rNET_tcp_loss | ✓ | ✓ | ✓ | 命令串含 `iptables -I ... DROP` | 可选 |
| rPROC_exit | ✓ | — | — | inject-only；output 无 record_id；clean/query 退出码 3 | 可选（kill 测试进程） |
| rPROC_hang | ✓ | ✓ | ✓ | `kill -STOP`；clean 发 `-CONT` | 可选 |
| rPROC_zstate | ✓ | ✓ | ✓ | 脚本 spawn 僵尸父进程 + pidfile | 可选 |
| rCPU_core_offline | ✓ | ✓ | ✓ | 命令串含 `echo 0 > .../online` | 可选（需 root） |
| rDISK_write_overload | ✓ | ✓ | ✓ | 脚本 spawn dd/fio + pidfile；clean 读 pidfile kill | 可选 |
| 全部 16 条 rNPU_* | ✓ | ✓ | ✓ | 命令串含 npu/<script>.sh；env 含 chip + 各故障参数 | 不做（仅 Atlas 物理机有 hccn_tool） |
| 注入器（builtin_injectors[]） | — | — | — | 本期为空，无覆盖；启用后用 `inj->op` 返回值断言 | — |

### 11.4 真实环境冒烟

- 可恢复故障：inject → query(active) → clean → query(empty) → 无残留进程。
- inject-only：inject → query(无记录) → clean 拒绝（退出码 3）。
- 手动 clean：inject → query(active) → clean → query(empty)。

---

## 12. 命令行与使用场景

### 12.1 命令结构

```
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

子命令模式：`inject` / `clean` / `query` / `list` / `serve` 为第一个参数，uid 为第二个位置参数（`query` 和 `list` 可省略；`serve` 不需 uid），其余为 `--key=value` 标志。

### 12.2 全局参数

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--config <path>` | 固定路径见 SPEC §7 | 配置文件路径覆盖 |
| `--plugins <dir>` | `<root>/plugins` | 动态插件目录覆盖（§3.13） |
| `--help` | — | 显示帮助 |

### 12.3 使用场景

#### 场景一：可恢复故障注入（手动 clean）

```bash
# 注入 CPU 过载 4 核（脚本 spawn yes 进程 + pidfile 后返回）
dcat inject rCPU_overload --cores=4

# 注入网络丢包 5%（脚本设 tc qdisc 后返回）
dcat inject rNET_loss --iface=eth0 --loss_pct=5

# 查询活跃
dcat query

# 手动 clean（按参数匹配，只清 eth0）
dcat clean rNET_loss --iface=eth0
```

#### 场景二：同 uid 不同资源并发 / 同资源重注入

```bash
# 同 uid 不同资源（iface 不同 → 无 overlap）允许并发（§8）
dcat inject rNET_loss --iface=eth0 --loss_pct=5
dcat inject rNET_loss --iface=eth1 --loss_pct=3

# 同资源重注入（iface 相同 → overlap）默认拒绝，需 --force 原子替换（§8）
dcat inject rNET_loss --iface=eth0 --loss_pct=8            # 退出码 5 拒绝
dcat inject rNET_loss --iface=eth0 --loss_pct=8 --force    # 清旧 eth0 记录后重注

# 按参数 clean（只清 eth0）
dcat clean rNET_loss --iface=eth0
```

#### 场景三：一次性故障（inject-only）

```bash
# 进程异常退出（不可恢复，无 clean）
dcat inject rPROC_exit --pid=12345

# query 看不到记录
dcat query

# clean 被拒绝（退出码 3）
dcat clean rPROC_exit
```

#### 场景四：查询与列表

```bash
dcat query rCPU_overload             # 单 uid 活跃记录
dcat query                           # 全部活跃记录
dcat list                            # 故障目录
```

#### 场景五：Web 控制台（serve）

```bash
# 只读模式启动 HTTP 控制平面
dcat serve --port 8080 --bind 0.0.0.0

# 可写模式（浏览器可注入/清理故障）
dcat serve --port 8080 --allow-write
```

---

## 13. 与 SPEC 的对应

- **决策1（注册机制）**：registry 从 cnf 载入 `fault_def`（含 6 个 per-op required/optional 字段）；编译 `injector_t` 作为进程内高级扩展点（§3.3 + §7）。
- **决策2（命令语法）**：子命令式 `dcat <subcommand> [uid] --key=value ...`，不再使用 SQL-like 单引号命令串；所有参数统一为 `--key=value` 标志（§3.2 + SPEC §2）。
- **决策3（故障机制）**：cnf + 脚本驱动（§1.2 + §4 + §6）；注入器为进程内回退路径（§7）。
- **决策4（参数传递）**：cnf 故障走环境变量不走 argv（§3.4 + §6）；注入器走 `params_t` 结构体指针（§7.3）。
- **决策5（必/选参数区分）**：按 op 分 required/optional 字段（`inject_required` / `inject_optional` / `clean_required` / `clean_optional` / `query_required` / `query_optional`）；precheck 按 op 取对应 `*_required` 校验，`*_optional` 缺省走脚本默认（§2 + §3.5 + SPEC §3.3）。
- **决策6（inject-only 故障）**：`supported_ops=inject` 的一次性故障不建 state、无 clean/query；dispatch 走 inject-only 分支（§3.9 + §5.1 + §4.2.3）。注入器同理（§7.3）。
- **决策7（发布批次）**：v0.1 起步（核心框架 + 58 条故障），后续按需扩充（SPEC §8）。
- **决策8（配置定位）**：固定相对路径 `<binary_dir>/../config/demoncat.conf`（通过 `/proc/self/exe` 解析）。conf 里的相对脚本路径在 `config_load` 时通过 `derive_project_root` + `resolve_script` 自动补成绝对路径，dcat 可从任意 CWD 运行（SPEC §7.1 + §3.7）。
- **决策9（不实现超时自动恢复）**：本期不实现 `duration` 参数、reaper 子进程、`auto_clean_loop` 后台线程、`state_lazy_clean`、`expires_at` 字段。所有可恢复故障注入后需用户手动 `clean`。cnf 与注入器故障均如此。
- **决策10（不实现安全确认）**：本期不实现 `safety` 字段、`safety_level_t` 枚举、`safety_confirm` 交互提示、`--yes` 全局 flag。预检只做静态校验。
- **决策11（统一同步阻塞执行）**：本期不区分 background/sync 模式，不实现 `executor_spawn`、`executor_kill`、`injection_record_t.bg_pid`。所有故障 inject/clean/query 均同步阻塞执行：cnf 故障用 `executor_run` / `executor_run_raw`，注入器故障直接调函数指针。需要长驻的故障由脚本自行 spawn 子进程 + 写 pidfile/sidecar 后立即返回；clean 重跑脚本读取清理。
- **决策12（注入器接口设计完成，实现留位）**：`injector_t` 接口（uid + 4 函数指针）、`builtin_injectors[]` 注册表、`injector_find` 查找、dispatch 回退路由均已设计（§7）。本期 `builtin_injectors[]` 为空数组，所有故障走 cnf+脚本路径；待出现脚本无法实现的需求（精确定时/二进制协议/进程内状态）时启用。
- **决策13（参数匹配与 clean）**：`injection_record_t` 存储 inject 时的 `params`，clean 按用户参数匹配活跃记录，传记录存储的 inject 参数给脚本，逐条执行；某条失败时停止，剩余不清理。**clean 和 query（带 uid）各自有独立的 `clean_required` / `query_required`：precheck 按 op 校验对应 required 列表齐全，缺参数被拒绝（退出码 3）；该 op 无 required 参数时允许空参数。** 不再有"至少一个参数"硬编码检查——是否需要参数完全由各 op 的 `*_required` 列表决定（precheck 自然处理）。query（不带 uid）查全部活跃记录不受此限制。所有命令（inject / clean / query）均拒绝未在对应 op 的 `*_required` / `*_optional` 中声明的参数（退出码 3），不做透传。
- **决策14（Reinject 默认拒绝 + --force 原子替换）**：对同一资源的重复注入默认拒绝（退出码 5），需 `--force` 才原子替换（逐条 clean 旧记录后重新 inject）。资源键 = `clean_required` 各参数值（`cores` 走集合交集，其余走精确串等，多参键取各参精确 AND）；inject-only 故障无 state 天然免检。CNF 路径覆盖 33 故障；插件 / legacy injector 路径 deferred。CPU `cores` 加法并集语义改为默认拒绝（有意 breaking）。详见 §8。
- **决策15（动态插件层）**：dispatch 第三层 `dlopen` 加载 `.so` 插件（`dcat_plugin_t` 接口 + ABI 版本门控 + per-op 参数声明 + 生命周期钩子）。三层优先级：cnf > 编译注入器 > 动态插件。默认插件目录 `<root>/plugins`，`--plugins <dir>` 覆盖。详见 [Dynamic_Plugin_Implement.md](Dynamic_Plugin_Implement.md) + §3.13。
- **决策16（Web 控制平面 serve）**：`dcat serve` 内置 HTTP 控制平面 + 静态前端，把故障目录/活跃注入/历史记录搬到浏览器。默认只读（`--allow-write` 开写），`realpath()` 路径穿越防护 + `%2e` 编码检测。详见 §3.12 + SPEC §12。
