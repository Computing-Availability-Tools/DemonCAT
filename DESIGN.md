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
- [4. 故障详设（按模块分组）](#4-故障详设按模块分组)
  - [4.1 网络模块（network）](#41-网络模块network11-条)
  - [4.2 进程模块（process）](#42-进程模块process4-条)
  - [4.3 CPU 模块（cpu）](#43-cpu-模块cpu2-条)
  - [4.4 存储模块（storage）](#44-存储模块storage1-条)
  - [4.5 NPU 模块（npu）](#45-npu-模块npu20-条)
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
- [8. 目录结构](#8-目录结构)
- [9. 构建](#9-构建)
- [10. 测试设计](#10-测试设计)
  - [10.1 mock_executor](#101-mock_executor)
  - [10.2 表驱动](#102-表驱动)
  - [10.3 故障覆盖矩阵](#103-故障覆盖矩阵)
  - [10.4 真实环境冒烟](#104-真实环境冒烟)
- [11. 命令行与使用场景](#11-命令行与使用场景)
  - [11.1 命令结构](#111-命令结构)
  - [11.2 全局参数](#112-全局参数)
  - [11.3 使用场景](#113-使用场景)
- [12. 与 SPEC 的对应](#12-与-spec-的对应)

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
   dispatch.c (按 op 分发: inject / clean / query / list)  (§3.9)
   ├─ cnf 故障    → executor_run/run_raw 调脚本
   └─ 注入器故障  → 直接调 injector_t 函数指针 (§7)
```

**核心层**（编译进二进制，稳定）：`main / cli / registry / executor / precheck / state / config / output / dispatch`。
**故障层**（可变，数据驱动）：`demoncat.conf` 声明 + 外部脚本（`src/scripts/<module>/*.sh`）。
**扩展点**：`injector_t` 编译注入器作为进程内高级扩展点，设计见 §7（本期 `builtin_injectors[]` 为空，仅留位）。

> 本期所有故障**统一同步阻塞执行**，不区分 background/sync 模式。

### 1.2 扩展机制：cnf + 脚本驱动

核心设计原则：**新增故障只需写脚本 + 在 `demoncat.conf` 加 `[fault.<uid>]` 段**，调度与解析逻辑自动发现，二进制不变。

扩展方式示例（新增一个内存故障）：

```ini
# demoncat.conf
[fault.rMEM_ecc_inject]
module          = memory
desc            = 内存 ECC 错误注入
script          = /usr/lib/demoncat/scripts/memory/mem_ecc_inject.sh
supported_ops   = inject,clean,query
required_params = dimm
optional_params =
```

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
     │                                  └─ 仍未命中 → 退出码 4
     ▼
   dispatch_route:
   ├── inject ──→ precheck(op∈supported_ops, required_params 齐全, 脚本/注入器可执行)
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
   └── list  ──→ registry_list → output catalog JSON
```

### 1.4 与编译注入器的关系

`registry_find` 先查 cnf 载入的 `fault_def` 表；未命中再查 `builtin_injectors[]`（`src/injectors/injector.h`，本期为空数组）。本期所有故障均走 cnf 路径。注入器接口设计与注册机制详见 §7。

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
    char required_params[128];   /* "iface,loss_pct" */
    char optional_params[128];   /* 可选参数名 */
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

> `fault_def_t` 不含 `safety` / `timeout` 字段（本期不实现安全确认、超时自动恢复）。预检只校验 `required_params`，`optional_params` 缺省时不报错。`inject`-only 故障不创建 `injection_record_t`。`injection_record_t` 含 `params` 字段（存储 inject 时用户参数，用于 clean 时按参数匹配记录；同 uid 不同参数允许并发）。`injection_record_t` 不含 `bg_pid`（统一同步执行，dcat 不托管子进程 pid）。

> 高级扩展点：`injector_t { uid, 4 个函数指针 inject/clean/query/precheck }` 注册到 `builtin_injectors[]`，registry 未在 cnf 命中时回退查找。完整设计见 §7。

---

## 3. 模块职责

### 3.1 main.c
读取配置（`config_load`，固定路径见 SPEC §7）→ 注册 `fault_def` 表到 registry → 读取 argv（subcommand + uid + flags，含 `--config` / `--help`）→ `cli_parse` → `registry_find`（未命中回退 `injector_find`，见 §7.4）→ 按 op 分发（`dispatch_route`）→ `output_print` → 返回退出码。

### 3.2 cli.c（命令解析器）
解析 `subcommand uid [flags]`，产出：
```c
typedef struct { const char *op; char uid[64]; params_t params; } parsed_cmd_t;
```
解析流程：
1. `argv[1]` = subcommand（`inject` / `clean` / `query` / `list`）；`--help` 直接输出帮助并退出
2. `argv[2]` = uid（若存在且不以 `--` 开头）；`query` 和 `list` 可省略 uid
3. 剩余 argv = `--key=value` 标志，逐对解析进 `params_t`（`--config` / `--help` 为全局选项，不进 params）

所有参数统一为 `--key=value` 标志，不再区分 inject 的 `(p1,p2) values (v1,v2)` 与 clean/query 的 `where k=v`。

错误返回解析错误 JSON（退出码 2）。

### 3.3 registry.c
- `registry_init(config)`：从 config 载入 `fault_def` 到静态数组。
- `const fault_def_t *registry_find(const char *uid)`：cnf 查找。
- `registry_list()`：返回全部 cnf 故障，供 `list` 命令。
- **注入器回退**：`registry_find` 未命中时，dispatch 转 `injector_find(uid)`（§7.4）查 `builtin_injectors[]`。本期为空数组，回退永不命中。

### 3.4 executor.c
```c
result_t *executor_run(const char *cmd, int timeout_ms);   /* 同步: fork/exec+pipe, timer_create 超时 */
int        executor_run_raw(const char *cmd);              /* 同步: system() 直通 stdout, 返回退出码 */
int        executor_check_tool(const char *path);           /* access/X_OK */
void       executor_set_mock(mock_fn fn);                   /* 测试钩子 */
```
- 命令构造：`build_cmd(fault_def, params, op)` 拼 `"<script>"`，参数通过**环境变量**传递（`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_<KEY>`），不走 argv，免注入。
- `executor_run`：用 `fork/exec + pipe` 同步执行，`timer_create` + `SIGCHLD` / `waitpid` 实现超时；超时返回错误。用于 cnf 故障的 inject / clean 路径——dcat 等待脚本执行完返回。
- `executor_run_raw`：用 `system()` 直接执行，stdout/stderr 继承父进程终端（不 pipe 捕获）。用于 cnf 故障的 query 路径——用户需要看到脚本的原始输出（表格/多行文本），而非 JSON 包装。返回脚本退出码（0=成功）。
- `mock_fn`：测试时注入，捕获 `(cmd, env)` 不真正执行。
- **仅服务 cnf 故障**：注入器故障不经过 executor，直接调函数指针（§7.5）。
- **不提供** `executor_spawn` / `executor_kill`：本期统一同步阻塞执行，dcat 不托管子进程 pid。需要长驻的故障由脚本自行 spawn 子进程 + 写 pidfile/sidecar，clean 时重跑脚本读取清理。

### 3.5 precheck.c
- `precheck(fault_def, op, params)`：执行 SPEC §4.2 预检 4 步；返回 `result_t`（成功 / 带错误码）。
- 本模块只做静态校验（uid 存在、op 合法、参数齐全、脚本可执行），**不做交互确认**（本期不实现 safety_confirm）。
- **参数校验边界**：precheck 校验 `required_params` 齐全且非空（**结构校验**，仅 inject）；参数值合法性（如 `cores` 范围、`iface` 是否存在）由脚本自行校验（**语义校验**）。所有命令（inject / clean / query）均校验用户提供的参数是否在 `required_params` / `optional_params` 中声明，未声明的参数直接拒绝（报错退出码 3）。
- **不限制并发注入**：允许同 uid 重复注入（含相同参数），dcat 不做并发拦截；脚本自行处理幂等性。
- 注入器故障的预检由 `injector_t->precheck` 函数指针实现（§7.3），不走本模块。

### 3.6 state.c
- 静态 `injection_record_t g_records[DCAT_MAX_RECORDS]` + `pthread_mutex_t`。
- `state_add(uid, params)` → 分配 `record_id`，写 `params` / `started_at` / `active=1`。**仅 `inject,clean,query` 故障调用**（cnf 与注入器均如此）。
- `state_find_by_params(uid, params)`：按 uid + 用户提供的参数匹配活跃记录（用户提供的参数须与 inject 时参数一致才匹配）。供 `clean` 使用。
- `state_find(uid)` / `state_find_by_id(id)` / `state_list()`。
- `state_mark_inactive(id)`：clean 后置 `active=0`。
- 持久化：变更后写 `~/.demoncat/state.json`（cJSON 序列化），启动时加载（恢复 `record_id` 计数与未清理记录，含 params）。
- **不实现**自动清理线程 / `state_lazy_clean` / `expires_at` / `bg_pid`（本期不实现超时自动恢复；clean 一律由用户手动调用；统一同步执行无 dcat 托管 pid）。
- **不区分故障来源**：cnf 故障与注入器故障共用同一 state 表。

### 3.7 config.c
INI 解析 `demoncat.conf`：
- `[demoncat]` 段：`state_file` / `log_level`。
- `[fault.<uid>]` 段：载入 `fault_def_t`（含 `optional_params`，不含 `safety` / `timeout`）。
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
按 op 路由，串联各模块（见 §5 关键流程）。`inject` 分支区分 `inject`-only 与可恢复；`clean` / `query` 对 `inject`-only 故障在 precheck 阶段拒绝（退出码 3）。

**按故障来源分流**（§7.5）：
- **cnf 故障**（`registry_find` 命中 `fault_def_t`）：inject/clean 走 `executor_run`，query 走 `executor_run_raw`。
- **注入器故障**（`injector_find` 命中 `injector_t`）：直接调 `inj->inject/clean/query` 函数指针，不 fork、不设环境变量。

- `dispatch_clean_record(rec)`：传记录存储的 inject 参数给脚本 `DCAT_OP=clean` 或调 `inj->clean(params)`。由 `dispatch_clean` 调用，按用户参数匹配活跃记录，逐条执行 clean。**无 background→executor_kill 分支**（本期统一同步执行，clean 始终由脚本/注入器自行清理其子进程与系统资源）。

---

## 4. 故障详设（按模块分组）

> 38 条目录。按模块分组详设，共享机制集中描述，差异以表呈现。
> 所有故障统一同步阻塞执行。需要长驻的故障（CPU 过载、端口占用、僵尸生成、磁盘写压等）由脚本自行 spawn 子进程 + 写 pidfile/sidecar 后立即返回；clean 重跑脚本读取 pidfile 清理。
> 本期 38 条故障均走 cnf+脚本路径；注入器路径（§7）留位待启用。

### 4.1 网络模块（network，11 条）

#### 4.1.1 共享机制

| 机制 | 命令 | 适用故障 | clean 策略 |
|---|---|---|---|
| `tc netem` | `tc qdisc add/replace dev <iface> root netem ...` | delay / loss / reorder / jitter | `tc qdisc del dev <iface> root`（幂等） |
| `tc tbf` | `tc qdisc add dev <iface> root tbf rate <rate>...` | bw_limit | `tc qdisc del` |
| `ip link` | `ip link set dev <iface> down/up` | down / link_flap | `ip link set up` |
| `ethtool` | `ethtool -s <iface> speed <n>` | degrade | `ethtool -s <iface> speed 1000`（或 autoneg on） |
| `iptables` | `iptables -I/-D INPUT/OUTPUT -p tcp --dport <port> -j DROP` | tcp_loss | `iptables -D` 删规则 |
| socket 占用 | python/socket 持有端口 | port_occupy | kill 持有进程（脚本读 pidfile） |
| systemctl | `systemctl stop/start <service>` | service_stop | `systemctl start` |

clean = dcat 重跑脚本 `DCAT_OP=clean`（传记录存储的 inject 参数）。脚本据此删 qdisc / 删 iptables 规则 / 起服务 / 读 pidfile kill 子进程。

#### 4.1.2 11 条故障差异表

| UID | 必填参数 | 可选参数 | clean 机制 | 备注 |
|---|---|---|---|---|
| `rNET_delay` * | iface,delay_ms | — | `tc qdisc del` | 示例 |
| `rNET_loss` | iface,loss_pct | — | `tc qdisc del` | `netem loss random <pct>%` |
| `rNET_reorder` | iface,reorder_pct | — | `tc qdisc del` | `netem reorder <pct>%` |
| `rNET_down` | iface | — | `ip link set up` | clean 后保证 up |
| `rNET_degrade` | iface | speed_mbps(默认10) | `ethtool -s speed 1000` 或 autoneg | speed_mbps 可选默认 10 |
| `rNET_port_occupy` | port | protocol(默认tcp) | kill 持有进程 | 脚本 spawn socket holder + pidfile，立即返回 |
| `rNET_service_stop` | service | — | `systemctl start <service>` | — |
| `rNET_link_flap` | iface | cycle_sec(默认2),count(默认10) | kill 闪断循环进程 + `ip link set up` | 脚本 spawn 循环进程 + pidfile，按 count 自结束 |
| `rNET_bw_limit` | iface,rate_kbps | — | `tc qdisc del` | `tbf rate <rate>kbps` |
| `rNET_jitter` | iface,delay_ms,jitter_ms | — | `tc qdisc del` | `netem delay <base> <jitter>` |
| `rNET_tcp_loss` | port | direction(默认both) | `iptables -D ...` | L4 丢包；direction ∈ {in,out,both} |

#### 4.1.3 错误处理

- `tc` / `iptables` / `ethtool` / `systemctl` 不存在或无权限时，脚本非 0 退出，stderr 报错，dcat 纳入 `error.message`，退出码 1。
- `iface` 不存在时 `tc qdisc add` 报错，脚本退出非 0。
- 长驻型故障（port_occupy / link_flap）脚本 clean 时读 pidfile kill 子进程后清理。

### 4.2 进程模块（process，4 条）

#### 4.2.1 共享机制

| 机制 | 命令/工具 | 适用故障 | 备注 |
|---|---|---|---|
| `kill` 信号 | `kill -9/-STOP/-CONT <pid>` | exit / hang | exit 不可逆；hang 可逆 |
| 不可中断 IO | C helper `open + ioctl(不可中断命令)` 或 `vhangup` | dstate | 触发 D 状态，需 root |
| 僵尸生成 | fork 子进程 + 子 exit + 父不 wait | zstate | count 个僵尸 |

#### 4.2.2 4 条故障差异表

| UID | supported_ops | 必填 | 可选 | clean 机制 | 备注 |
|---|---|---|---|---|---|
| `rPROC_exit` | **inject** | pid | — | **N/A** | `kill -9 <pid>`，不可恢复；**唯一 inject-only 故障** |
| `rPROC_dstate` | inject,clean,query | count | — | kill D 态进程 | 脚本 spawn count 个 D 态 helper + pidfile，立即返回 |
| `rPROC_hang` | inject,clean,query | pid | — | `kill -CONT <pid>` | `kill -STOP`；clean 重跑脚本发 CONT |
| `rPROC_zstate` | inject,clean,query | count | — | kill 父进程（子被 init reap） | 脚本 spawn count 僵尸父进程 + pidfile，立即返回 |

#### 4.2.3 rPROC_exit 的 inject-only 语义

- `supported_ops = inject`：目录中不声明 `optional_params`。
- dispatch 走 inject-only 分支：执行 → `output_ok(message)`，**不写 state**。
- `dcat clean rPROC_exit` / `dcat query rPROC_exit` 在 precheck 阶段拒绝（op 不在 supported_ops，退出码 3）。

### 4.3 CPU 模块（cpu，2 条）

#### 4.3.1 共享机制

| UID | 机制 | 命令 | clean |
|---|---|---|---|
| `rCPU_overload` * | 多核 burn | `yes` 多实例 | kill 进程组（脚本读 pidfile） |
| `rCPU_core_offline` | sysfs 离线 | `echo 0 > /sys/devices/system/cpu/cpu<N>/online` | `echo 1 > ...` |

#### 4.3.2 rCPU_core_offline 详设

- **必填**：`cores`（格式 `"0,2,4"` 或 `"0-3"`，脚本解析）。
- 脚本 `for n in cores; do echo 0 > .../cpu<N>/online; done`，执行完返回。clean = 重跑脚本 `DCAT_OP=clean`，对所有 cores `echo 1`。
- **限制**：cpu0 通常不可离线（内核 CONFIG_BOOTPARAM_HOTPLUG_CPU0），脚本须跳过并 stderr 提示。

### 4.4 存储模块（storage，1 条）

#### 4.4.1 rDISK_write_overload

- **必填**：`device`（块设备路径，如 `/dev/sda` 或挂载点 `/data`）。
- **可选**：`workers`（默认 4）。
- 脚本 spawn N 个 `dd if=/dev/zero of=<device>/dcat.stress bs=1M` 或 `fio --rw=write --numjobs=N`，写 pidfile 后立即返回。
- **clean** = 重跑脚本 `DCAT_OP=clean`：读 pidfile kill 进程组，dd/fio 自然终止；可选 `rm -f <device>/dcat.stress`。
- **预检**：`device` 存在且可写；`fio` 或 `dd` 可执行。

### 4.5 NPU 模块（npu，20 条）

#### 4.5.1 共享机制

所有 NPU 故障通过 `hccn_tool -i <chip> <op>` 操作 RoCE 网卡。脚本共享 `src/scripts/npu/_common.sh`（`npu_check_env`/`sidecar_save/load/clear`），每脚本内联 `fault_present` 函数实现 query-then-clean 幂等。

| clean 策略 | 适用 UID | 机制 |
|---|---|---|
| 反向操作 | arp_poison, route_add, iprule_add, iproute_add | del 加的 / add 删的 |
| sidecar 回放 | ip_change, gw_change, netdetect_change, arp_del, route_del, iprule_del, iproute_del, mtu, fec, dscp_tc, prio_tc, pfc, roce_port | inject 前 -g 存原值；clean 回放 |
| 设回 max | bw_limit | clean = -shaping -s bw_limit 100000 |
| -cfg recovery | link_down, route_clear | hccn_tool 内置恢复 |

#### 4.5.2 20 条故障差异表

（见 SPEC §3.3 完整目录表）

#### 4.5.3 约束

- 所有 NPU 故障同步执行（hccn_tool 是快命令，执行完返回）。
- 所有故障 `required_params` 含 `chip`（hccn_tool 的 `-i <0~7>`）。
- 真实环境冒烟仅华为 Atlas 物理机可做（CI mock-only，与网络类一致）。

---

## 5. 关键流程

### 5.1 inject（含 inject-only 分支 + 注入器回退）

```
parse → registry_find(uid)
  ├─ cnf 命中 fault_def:
  │     → precheck(op∈supported_ops, required_params 齐全, 脚本可执行)
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
  │     └─ 未命中 → output_err(4, "not found")
```

### 5.2 clean

```
parse → registry_find(uid)
  ├─ cnf 命中 fault_def:
  │     → precheck(op=clean ∈ supported_ops)     # inject-only 故障在此拒绝 (退出码 3)
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
  - cnf 故障：`executor_run_raw(DCAT_OP=query)`，脚本 stdout 原样输出到终端。
  - 注入器故障：`inj->query(params)`，函数返回的 result_t 中携带证据文本。
  - dcat 打印 `---` 分隔符后输出 JSON `{"confirmed":true/false}`。inject-only 故障在 precheck 拒绝（退出码 3）。
- **list**：`registry_list()`，输出 cnf fault 目录 JSON（含 supported_ops / required_params / optional_params / desc）。注入器故障本期不纳入 list 输出。

---

## 6. 脚本契约（实现约定）

### 6.1 通用约定

- 环境变量传参：`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_<KEY>`（KEY 大写、非字母数字→`_`）。
- 退出码 `0`=成功；非 `0`=失败。
- stdout 成功文本 → `data.message`；stderr → `error.message`。
- 可选参数未提供时，对应 `DCAT_PARAM_<KEY>` 环境变量不设置；脚本须自行处理默认值（目录表中 `(默认X)` 标注）。
- **同步阻塞**：dcat `fork/exec + waitpid` 等待脚本执行完返回。脚本**不应前台驻留阻塞 dcat**；需要长驻的故障由脚本自行 spawn 子进程 + 写 pidfile/sidecar（如 `/tmp/dcat-<uid>.pid`）后立即返回。

### 6.2 可恢复故障脚本

- inject 脚本执行完即返回。需要长驻的故障（CPU 过载、端口占用、僵尸生成、磁盘写压等）自行 spawn 子进程 + 写 pidfile/sidecar 后立即返回。
- clean 时 dcat 按用户参数匹配活跃记录，传记录存储的 inject 参数给脚本 `DCAT_OP=clean`，逐条执行。某条失败时停止，剩余不清理。脚本据此清理资源（删 qdisc / 删 iptables 规则 / 起服务 / 读 pidfile kill 子进程）。脚本退出码非 0 时 dcat 报错且**不 mark inactive**（故障可能仍在系统上）。
- **无 uid 的 query** 由 dcat 状态回答，脚本无需实现该路径；**有 uid 的 query** 由 dcat 调脚本 `DCAT_OP=query` 分支验证（参数为用户当前输入，非 inject 时参数）。脚本 stdout 原样输出 + `---` + JSON。

### 6.3 inject-only 故障脚本

- 仅实现 `DCAT_OP=inject` 分支；不期望 `DCAT_OP=clean`。
- 目录中不声明 `optional_params`。
- inject 完成即终结，不写 state。

### 6.4 默认值约定

- 目录表中 `(默认X)` 表示该可选参数缺省时的脚本内部默认值。
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
  fault = registry_find(uid)            # cnf 优先
  if fault != NULL:
      → precheck(fault, op, params)     # §3.5
      → executor_run / executor_run_raw # §3.4 调脚本 (现有 cnf 流程)
  else:
      inj = injector_find(uid)          # 回退注入器 (§7.4)
      if inj != NULL:
          → inj->precheck(op, params)   # 注入器自带预检
          → inj->op(params)            # 直接函数调用, 不 fork/不设 env
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
| 超时 | `executor_run` 的 `timer_create` | 注入器自行实现 |
| 适用场景 | 大多数故障（tc/iptables/kill 等） | 精确定时/二进制协议/进程内状态 |
| state 记录 | 可恢复故障写 state | 同左 |
| list 输出 | 纳入 | 本期不纳入（注入器本期为空） |
| 优先级 | `registry_find` 优先 | `injector_find` 回退 |

> **YAGNI**：本期 `builtin_injectors[]` 为空数组，仅头文件 `injector.h` 与 `injectors.c` 留位。所有 38 条故障均走 cnf+脚本路径。注入器接口设计完成，待后续有脚本无法实现的需求时启用。

---

## 8. 目录结构

```
CAT/
├── CMakeLists.txt              # C11, 静态链接, 含 third_party/cjson
├── SPEC.md  DESIGN.md  README.md
├── third_party/cjson/{cJSON.c,cJSON.h}
├── src/
│   ├── main.c
│   ├── core/
│   │   ├── cli.{c,h}
│   │   ├── registry.{c,h}
│   │   ├── executor.{c,h}
│   │   ├── precheck.{c,h}
│   │   ├── state.{c,h}
│   │   ├── config.{c,h}
│   │   ├── output.{c,h}
│   │   ├── dispatch.{c,h}
│   │   └── types.h             # 公共类型: params_t/result_t/fault_def_t(含 optional_params)/injection_record_t
│   └── injectors/
│       ├── injector.h          # 注入器接口 injector_t (§7.2)
│       └── injectors.c          # builtin_injectors[] 注册表 + injector_find (§7.4, 本期为空)
│   └── scripts/                    # 故障脚本源码（数据驱动，非编译，git chmod +x）
│       ├── cpu/                    # 2 脚本
│       │   ├── cpu_overload.sh        # 示例
│       │   └── cpu_core_offline.sh
│       ├── network/                # 11 脚本
│       │   ├── net_delay.sh           # 示例
│       │   ├── net_loss.sh
│       │   ├── net_reorder.sh
│       │   ├── net_down.sh
│       │   ├── net_degrade.sh
│       │   ├── net_port_occupy.sh
│       │   ├── net_service_stop.sh
│       │   ├── net_link_flap.sh
│       │   ├── net_bw_limit.sh
│       │   ├── net_jitter.sh
│       │   └── net_tcp_loss.sh
│       ├── process/                # 4 脚本
│       │   ├── proc_exit.sh
│       │   ├── proc_dstate.sh
│       │   ├── proc_hang.sh
│       │   └── proc_zstate.sh
│       ├── storage/                # 1 脚本
│       │   └── disk_write_overload.sh
│       └── npu/                    # 20 脚本 + _common.sh helper
│           ├── _common.sh
│           ├── link_down.sh
│           ├── ip_change.sh
│           ├── gw_change.sh
│           ├── netdetect_change.sh
│           ├── arp_poison.sh
│           ├── arp_del.sh
│           ├── route_add.sh
│           ├── route_del.sh
│           ├── route_clear.sh
│           ├── iprule_add.sh
│           ├── iprule_del.sh
│           ├── iproute_add.sh
│           ├── iproute_del.sh
│           ├── bw_limit.sh
│           ├── mtu_mismatch.sh
│           ├── fec_change.sh
│           ├── dscp_tc_change.sh
│           ├── prio_tc_change.sh
│           ├── pfc_change.sh
│           └── roce_port_change.sh
├── config/
│   └── demoncat.conf           # 故障目录配置(38 条)
└── tests/
    ├── test_cli.c
    ├── test_registry.c
    ├── test_executor_mock.c
    ├── test_precheck.c
    ├── test_state.c
    ├── test_output.c
    ├── test_faults.c               # 通用表驱动
    ├── test_faults_network.c      # 11 条网络故障
    ├── test_faults_process.c      # 4 条进程故障
    ├── test_faults_cpu_storage.c  # 2 条 CPU + 1 条存储故障
    └── test_faults_npu.c          # 20 条 NPU 故障
```

> `src/injectors/` 目录含 `injector.h`（接口定义，§7.2）与 `injectors.c`（注册表 + 查找，§7.4）。本期 `builtin_injectors[]` 为空数组。

---

## 9. 构建

- `CMakeLists.txt`：`set(CMAKE_C_STANDARD 11)`（gnu11 扩展开启，便于 `usleep`/`select`/`fork`），静态链接，`find_package(Threads)`，把 `third_party/cjson/cJSON.c` 与 `src/injectors/injectors.c` 编进二进制，`-Wall -Wextra -Werror`。
- 目标 `dcat`；测试通过 `enable_testing()` + `add_test`，`ctest` 驱动。测试 `WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}` 以便 `config/demoncat.conf` 与 `src/scripts/*.sh` 解析。
- WSL 验证：`cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure`。

---

## 10. 测试设计

> 开发遵循 TDD：先写测试用例（定义期望命令串 + 环境变量 + 退出码 + JSON 输出），再实现功能代码使测试通过。测试用例是行为的权威定义（见 SPEC §9.1）。

### 10.1 mock_executor

`executor_set_mock(fn)`，`fn` 捕获 `(cmd, env)` 不真正 fork。测试可断言 cnf 故障下发命令串与环境变量集合（`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_*`）。

> 注入器故障不经过 executor，mock_executor 不适用；注入器测试直接断言 `inj->op(params)` 返回的 `result_t`。

### 10.2 表驱动

以 `struct { input; fault_def; expect_cmd; expect_env; expect_record; expect_json; expect_exit; }` 数组驱动 inject / clean / query（cnf 故障）。

### 10.3 故障覆盖矩阵

| 故障 | inject | clean | query | mock 断言 | 真实冒烟 |
|---|:---:|:---:|:---:|---|---|
| rNET_loss / reorder / bw_limit / jitter | ✓ | ✓ | ✓ | 命令串含 `tc qdisc add ... netem/tbf`；env 含 iface/... | 可选（需 root + iface） |
| rNET_down | ✓ | ✓ | ✓ | 命令串含 `ip link set down` | 可选 |
| rNET_degrade | ✓ | ✓ | ✓ | 命令串含 `ethtool -s` | 可选 |
| rNET_port_occupy | ✓ | ✓ | ✓ | env 含 port/protocol；脚本 spawn holder + pidfile | 可选 |
| rNET_service_stop | ✓ | ✓ | ✓ | 命令含 `systemctl stop` | 可选 |
| rNET_link_flap | ✓ | ✓ | ✓ | 脚本 spawn 循环进程 + pidfile；按 count 自结束 | 可选 |
| rNET_tcp_loss | ✓ | ✓ | ✓ | 命令串含 `iptables -I ... DROP` | 可选 |
| rPROC_exit | ✓ | — | — | inject-only；output 无 record_id；clean/query 退出码 3 | 可选（kill 测试进程） |
| rPROC_dstate | ✓ | ✓ | ✓ | 脚本 spawn helper + pidfile | 可选（需 root + helper） |
| rPROC_hang | ✓ | ✓ | ✓ | `kill -STOP`；clean 发 `-CONT` | 可选 |
| rPROC_zstate | ✓ | ✓ | ✓ | 脚本 spawn 僵尸父进程 + pidfile | 可选 |
| rCPU_core_offline | ✓ | ✓ | ✓ | 命令串含 `echo 0 > .../online` | 可选（需 root） |
| rDISK_write_overload | ✓ | ✓ | ✓ | 脚本 spawn dd/fio + pidfile；clean 读 pidfile kill | 可选 |
| 全部 20 条 rNPU_* | ✓ | ✓ | ✓ | 命令串含 npu/<script>.sh；env 含 chip + 各故障参数 | 不做（仅 Atlas 物理机有 hccn_tool） |
| 注入器（builtin_injectors[]） | — | — | — | 本期为空，无覆盖；启用后用 `inj->op` 返回值断言 | — |

### 10.4 真实环境冒烟

- 可恢复故障：inject → query(active) → clean → query(empty) → 无残留进程。
- inject-only：inject → query(无记录) → clean 拒绝（退出码 3）。
- 手动 clean：inject → query(active) → clean → query(empty)。

---

## 11. 命令行与使用场景

### 11.1 命令结构

```
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

子命令模式：`inject` / `clean` / `query` / `list` 为第一个参数，uid 为第二个位置参数（`query` 和 `list` 可省略），其余为 `--key=value` 标志。

### 11.2 全局参数

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--config <path>` | 固定路径见 SPEC §7 | 配置文件路径覆盖 |
| `--help` | — | 显示帮助 |

### 11.3 使用场景

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

#### 场景二：并发注入同 uid 不同参数

```bash
# 同 uid 不同参数允许并发
dcat inject rNET_loss --iface=eth0 --loss_pct=5
dcat inject rNET_loss --iface=eth1 --loss_pct=3

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

---

## 12. 与 SPEC 的对应

- **决策1（注册机制）**：registry 从 cnf 载入 `fault_def`（含 `optional_params`）；编译 `injector_t` 作为进程内高级扩展点（§3.3 + §7）。
- **决策2（命令语法）**：子命令式 `dcat <subcommand> [uid] --key=value ...`，不再使用 SQL-like 单引号命令串；所有参数统一为 `--key=value` 标志（§3.2 + SPEC §2）。
- **决策3（故障机制）**：cnf + 脚本驱动（§1.2 + §4 + §6）；注入器为进程内回退路径（§7）。
- **决策4（参数传递）**：cnf 故障走环境变量不走 argv（§3.4 + §6）；注入器走 `params_t` 结构体指针（§7.3）。
- **决策5（必/选参数区分）**：`required_params` 预检校验；`optional_params` 缺省走脚本默认（§2 + §3.3 SPEC）。
- **决策6（inject-only 故障）**：`supported_ops=inject` 的一次性故障不建 state、无 clean/query；dispatch 走 inject-only 分支（§3.9 + §5.1 + §4.2.3）。注入器同理（§7.3）。
- **决策7（发布批次）**：v0.1 起步（核心框架 + 38 条故障），后续按需扩充（SPEC §8）。
- **决策8（配置定位）**：固定相对路径 `<binary_dir>/../config/demoncat.conf`（通过 `/proc/self/exe` 解析）。conf 里的相对脚本路径在 `config_load` 时通过 `derive_project_root` + `resolve_script` 自动补成绝对路径，dcat 可从任意 CWD 运行（SPEC §7.1 + §3.7）。
- **决策9（不实现超时自动恢复）**：本期不实现 `duration` 参数、reaper 子进程、`auto_clean_loop` 后台线程、`state_lazy_clean`、`expires_at` 字段。所有可恢复故障注入后需用户手动 `clean`。cnf 与注入器故障均如此。
- **决策10（不实现安全确认）**：本期不实现 `safety` 字段、`safety_level_t` 枚举、`safety_confirm` 交互提示、`--yes` 全局 flag。预检只做静态校验。
- **决策11（统一同步阻塞执行）**：本期不区分 background/sync 模式，不实现 `executor_spawn`、`executor_kill`、`injection_record_t.bg_pid`。所有故障 inject/clean/query 均同步阻塞执行：cnf 故障用 `executor_run` / `executor_run_raw`，注入器故障直接调函数指针。需要长驻的故障由脚本自行 spawn 子进程 + 写 pidfile/sidecar 后立即返回；clean 重跑脚本读取清理。
- **决策12（注入器接口设计完成，实现留位）**：`injector_t` 接口（uid + 4 函数指针）、`builtin_injectors[]` 注册表、`injector_find` 查找、dispatch 回退路由均已设计（§7）。本期 `builtin_injectors[]` 为空数组，所有故障走 cnf+脚本路径；待出现脚本无法实现的需求（精确定时/二进制协议/进程内状态）时启用。
- **决策13（参数匹配与并发注入）**：允许同 uid 重复注入（含相同参数），dcat 不做并发拦截，脚本自行处理幂等性。`injection_record_t` 存储 inject 时的 `params`，clean 按用户参数匹配活跃记录，传记录存储的 inject 参数给脚本，逐条执行；某条失败时停止，剩余不清理。预检从 5 步减为 4 步（删除原"无活跃注入"并发检查）。所有命令（inject / clean / query）均拒绝未在 `required_params` / `optional_params` 中声明的参数（退出码 3），不做透传。
