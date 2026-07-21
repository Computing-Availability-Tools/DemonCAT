# DemonCAT 设计文档 (DESIGN)

> 对应 [SPEC.md](SPEC.md)。描述分层架构、数据结构、模块职责、按模块分组的故障详设、关键流程、脚本契约、目录结构、构建、测试设计与命令行场景。

---

## 1. 架构设计

### 1.1 分层架构

```
        ┌──────────────────────────────────────────────┐
        │ main.c  (流程编排: 读配置→解析→调度→输出)        │
        └───────────────┬──────────────────────────────┘
        │ cli.c (递归下降解析器, 产出 parsed_cmd_t)
        ▼
   registry.c (从 demoncat.conf 载入 fault_def 表; 按 uid 查找)
        │
   ┌────┴─────┬──────────┬──────────┬──────────┬──────────┐
   ▼          ▼          ▼          ▼          ▼          ▼
 executor   safety     state      config    output     (injectors:
 (sync/     (confirm+  (records+  (INI 解析  (JSON      见 §11
  spawn+    precheck)   auto-      + fault    schema)
  kill+     (§3.4)      clean)     表)       (§3.7)
  mock)     (§3.5)     (§3.6)     (§3.7)    
   (§3.4)                                    
        ▼
   dispatch.c (按 op 分发: inject / clean / query / list)
```

**核心层**（编译进二进制，稳定）：`main / cli / registry / executor / safety / state / config / output / dispatch`。
**故障层**（可变，数据驱动）：`demoncat.conf` 声明 + 外部脚本（`config/scripts/<module>/*.sh`）。
**扩展点**：`injector_t` 编译注入器仅作高级扩展点留位（§11）。

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
optional_params = duration
safety          = warning
exec_mode       = sync
```

对应脚本放到 `config/scripts/memory/mem_ecc_inject.sh` 并 `chmod +x`。`dcat list` 自动出现新故障，**免重编译**。

### 1.3 数据流

```
  argv (单条命令串)
     │
     ▼
  cli_parse  ──→  parsed_cmd_t { op, uid, params }
     │
     ▼
  registry_find(uid)  ──→  fault_def_t *  (or NULL → 退出码 4)
     │
     ▼
   dispatch_route:
    ┌── inject ──→ safety_precheck → safety_confirm
    │              ├─ inject-only (rPROC_exit): executor_run → output_ok (无 state)
    │              └─ 可恢复:
    │                 ├─ background: executor_spawn → state_add(uid,pid,duration)
    │                 └─ sync:      executor_run  → 若 duration>0: state_add(uid,0,duration)
    │                 → output_ok(message, record_id)
    │                 → 若 duration>0: dispatch_build_reaper + executor_spawn (detached reaper)
    ├── clean ──→ safety_precheck(op∈supported_ops) → state_find
   │              ├─ background: executor_kill(pid)
   │              └─ sync:      executor_run(DCAT_OP=clean, 回放 where)
   │              → state_mark_inactive → output_ok
   ├── query ──→ state_list / state_find → output records JSON (不调脚本)
   └── list  ──→ registry_list → output catalog JSON
```

### 1.4 与编译注入器的关系

`registry_find` 先查 cnf 载入的 `fault_def` 表；未命中再查 `builtin_injectors[]`（`src/injectors/injector.h`，本期为空数组）。本期所有故障均走 cnf 路径。

---

## 2. 数据结构

```c
/* types.h — 公共类型 */

/* params_t: 栈上, 统一承载两种参数语法 */
#define DCAT_MAX_PARAMS 16
#define DCAT_KEY_LEN    32
#define DCAT_VAL_LEN    64
typedef struct { char key[DCAT_KEY_LEN]; char value[DCAT_VAL_LEN]; } param_kv_t;
typedef struct { param_kv_t items[DCAT_MAX_PARAMS]; int count; } params_t;

/* result_t: 输出边界, json 由 cJSON 堆分配, 调用方 result_free */
typedef struct { int code; char *json; } result_t;

/* safety_level_t */
typedef enum { SAFETY_NORMAL, SAFETY_WARNING, SAFETY_DANGEROUS } safety_level_t;

/* exec_mode_t */
typedef enum { EXEC_SYNC, EXEC_BACKGROUND } exec_mode_t;

/* fault_def: 由 config.c 从 demoncat.conf 载入; registry 持有表 */
typedef struct {
    char uid[64];
    char module[32];
    char desc[128];
    char script[256];
    char supported_ops[64];      /* "inject" | "inject,clean,query" */
    char required_params[128];   /* "iface,loss_pct" */
    char optional_params[128];   /* "duration" — v0.2 新增 */
    safety_level_t safety;
    exec_mode_t   exec_mode;
    int timeout;                 /* 目录级 hint; v0.2 实际由 inject 时的 duration 参数驱动 (见 SPEC §3.2/§7.2) */
} fault_def_t;

/* injection_record_t: state 持有, 固定数组 — 仅 inject,clean,query 故障创建 */
typedef struct {
    int  record_id;             /* 单调递增 */
    char uid[64];
    pid_t bg_pid;               /* background 模式下 dcat 托管的 pid, 0=同步 */
    time_t started_at;
    time_t expires_at;          /* 0=不自动到期 */
    int active;                 /* 1 活跃, 0 已清理 */
} injection_record_t;
#define DCAT_MAX_RECORDS 32
```

> **v0.2 变更**：`fault_def_t` 新增 `optional_params` 字段；预检只校验 `required_params`，`optional_params` 缺省时不报错。`inject`-only 故障不创建 `injection_record_t`。

> 高级扩展点（本期不实现）：`injector_t { uid, 4 个函数指针 inject/clean/query/precheck }` 注册到 `builtin_injectors[]`，registry 未在 cnf 命中时回退查找。

---

## 3. 模块职责

### 3.1 main.c
读取配置（`config_load`，固定路径见 SPEC §7）→ 注册 `fault_def` 表到 registry → 启动 state 后台自动清理线程（含启动期 `state_lazy_clean`）→ 读取 argv（单条命令串或 `--config` / `--yes` / `--help`）→ `cli_parse` → `registry_find` → 按 op 分发（`dispatch_route`）→ `output_print` → 返回退出码。

> **Reaper 派生**（SPEC §4.3 路径一）：inject 成功且 `duration > 0` 时，main.c 在 `dispatch_inject` 返回后，通过 `dispatch_build_reaper(exe, uid, cfgpath, dur, buf, len)` 构造命令 `sleep <dur>; <exe> "clean <uid>" --config <cfg> --yes`，再 `executor_spawn` 派生为 detached 子进程。reaper 存活到 duration 到期，自己醒来执行 `dcat clean`，实现 one-shot CLI 退出后的自动恢复。

### 3.2 cli.c（递归下降解析器）
解析 `command uid [params]`，产出：
```c
typedef struct { const char *op; char uid[64]; params_t params; } parsed_cmd_t;
```
两个 params 产出式汇入同一 `params_t`：
- `(p1,p2) values (v1,v2)`：按序 key/value 配对
- `where k1=v1 k2=v2`：空格分隔的 k=v 对

错误返回解析错误 JSON（退出码 2）。

### 3.3 registry.c
- `registry_init(config)`：从 config 载入 `fault_def` 到静态数组。
- `const fault_def_t *registry_find(const char *uid)`。
- `registry_list()`：返回全部，供 `list` 命令。
- 高级扩展点回退：未命中 cnf 时查 `builtin_injectors[]`（本期为空）。

### 3.4 executor.c
```c
result_t *executor_run(const char *cmd, int timeout_ms);   /* 同步: fork/exec+pipe, timer_create 超时 */
int        executor_run_raw(const char *cmd);              /* 同步: system() 直通 stdout, 返回退出码 */
pid_t      executor_spawn(const char *cmd);                 /* 后台: fork, 返回 pid */
int        executor_kill(pid_t pid);                        /* SIGTERM→SIGKILL, 整进程组 */
int        executor_check_tool(const char *path);           /* access/X_OK */
void       executor_set_mock(mock_fn fn);                   /* 测试钩子 */
```
- 命令构造：`build_cmd(fault_def, params, op)` 拼 `"<script>"`，参数通过**环境变量**传递（`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_<KEY>`），不走 argv，免注入。
- `executor_run` 用 `timer_create` + `SIGCHLD` / `waitpid` 实现超时；超时返回错误。
- `executor_run_raw`：用 `system()` 直接执行，stdout/stderr 继承父进程终端（不 pipe 捕获）。用于 query 路径——用户需要看到脚本的原始输出（表格/多行文本），而非 JSON 包装。返回脚本退出码（0=成功）。
- `executor_spawn`：fork 后子进程 `setsid()` 创建新会话（脱离控制终端），并将 stdin/stdout/stderr 重定向到 `/dev/null`。这确保后台进程（如 reaper）不会向前台终端输出，避免污染用户交互。父进程不 wait，立即返回 pid。
- `mock_fn`：测试时注入，捕获 `(cmd, env)` 不真正执行。

### 3.5 safety.c
- `safety_confirm(safety_level_t)`：dangerous 需 `yes`；warning 需 `y`（默认 N）。非交互场景（无 tty）下 dangerous 直接拒绝。`--yes` 全局 flag 跳过 warning 确认（不跳过 dangerous）。
- `safety_precheck(fault_def, op, params)`：执行 SPEC §4.2 预检 5 步；返回 `result_t`（成功 / 带错误码）。`inject`-only 故障跳过第 5 步（无并发约束）。

### 3.6 state.c
- 静态 `injection_record_t g_records[DCAT_MAX_RECORDS]` + `pthread_mutex_t`。
- `state_add(uid, bg_pid, duration)` → 分配 `record_id`，写 `started_at` / `expires_at`（`duration>0` 时）/ `active=1`。**仅 `inject,clean,query` 故障调用**。
- `state_find(uid)` / `state_find_by_id(id)` / `state_list()`。
- `state_mark_inactive(id)`：clean 后置 `active=0`。
- `state_lazy_clean()`：启动期同步扫描 `expires_at <= now && active`，对到期记录执行 clean（background→`executor_kill(pid)`；sync→重跑脚本 `DCAT_OP=clean`），置 inactive。解决 one-shot CLI 进程退出后线程不存活问题。
- 后台线程 `auto_clean_loop`：每秒扫到期记录同上；用于 long-run 场景与 `test_autoclean`。
- `state_set_clean_cb(fn)`：注入 clean 回调（dispatch 注册，使 state 模块不依赖 executor）。
- 持久化：变更后写 `~/.demoncat/state.json`（cJSON 序列化），启动时加载（恢复 `record_id` 计数与未到期记录；已过期进程型故障保守标记 inactive）。

### 3.7 config.c
INI 解析 `demoncat.conf`：
- `[demoncat]` 段：`state_file` / `log_level`。
- `[fault.<uid>]` 段：载入 `fault_def_t`（含 v0.2 新增 `optional_params`）。
- 固定路径见 SPEC §7；`--config` 覆盖。
- **项目根推导**：`derive_project_root(cfgpath)` 从配置路径反推项目根（`<root>/config/demoncat.conf` → `<root>`）。
- **脚本路径解析**：`resolve_script(root, val, dst, cap)` 对相对脚本路径 prepend 项目根，使其变为绝对路径，让 dcat 可从任意 CWD 运行。已绝对（`/` 开头）或 home 相对（`~` 开头）的路径保持原样。当 root 为 `.`（测试用相对配置路径加载）时也保持原样，走 CWD 解析。
- 输出 `config_t { state_file, log_level, fault_def_t faults[], int fault_count }`。

### 3.8 output.c
- `result_t *result_ok(op, uid, cJSON *data)` / `result_err(op, uid, code, msg)`。
- `output_print(result_t*)`：JSON 打印到 stdout，`result_free` 释放。
- 统一 schema 见 SPEC §6。`inject`-only 的 `result_ok` 不写 `record_id` 字段。

### 3.9 dispatch.c
按 op 路由，串联各模块（见 §5 关键流程）。`inject` 分支区分 `inject`-only 与可恢复；`clean` / `query` 对 `inject`-only 故障在 precheck 阶段拒绝（退出码 3）。

- `dispatch_build_reaper(exe, uid, cfgpath, dur, buf, len)`：构造 auto-recovery reaper 命令 `sleep <dur>; <exe> "clean <uid>" --config <cfg> --yes`。`clean <uid>` **必须用引号包裹**，确保 shell 传给 dcat 时是单个 argv 元素（否则 main.c 的 argv 解析会把 `clean` 和 `<uid>` 当两个参数，cmdarg 只保留最后一个 → parse error）。
- `dispatch_clean_record(rec)`：state 自动清理回调，按记录的 `bg_pid` 决定 clean 策略（background → `executor_kill`；sync → 重跑脚本 `DCAT_OP=clean`）。

---

## 4. 故障详设（按模块分组）

> v0.1 已有 2 条示例 + v0.2 新增 16 条 + v0.3 新增 20 条 npu = 38 条目录。按模块分组详设，共享机制集中描述，差异以表呈现。

### 4.1 网络模块（network，10 条新增 + 已有 rNET_delay）

#### 4.1.1 共享机制

| 机制 | 命令 | 适用故障 | clean 策略 |
|---|---|---|---|
| `tc netem` | `tc qdisc add/replace dev <iface> root netem ...` | delay / loss / reorder / jitter | `tc qdisc del dev <iface> root`（幂等） |
| `tc tbf` | `tc qdisc add dev <iface> root tbf rate <rate>...` | bw_limit | `tc qdisc del` |
| `ip link` | `ip link set dev <iface> down/up` | down / link_flap | `ip link set up` |
| `ethtool` | `ethtool -s <iface> speed <n>` | degrade | `ethtool -s <iface> speed 1000`（或 autoneg on） |
| `iptables` | `iptables -I/-D INPUT/OUTPUT -p tcp --dport <port> -j DROP` | tcp_loss | `iptables -D` 删规则 |
| socket 占用 | python/socket 持有端口 | port_occupy | kill 持有进程 |
| systemctl | `systemctl stop/start <service>` | service_stop | `systemctl start` |

sync 模式 clean = dcat 重跑脚本 `DCAT_OP=clean`（回放原 `where` 参数，脚本据此删 qdisc / 删 iptables 规则 / 起服务）。
background 模式 clean = `executor_kill`（kill 持端口 / 闪断循环的进程）。

#### 4.1.2 10 条新增故障差异表

| UID | 必填参数 | 可选参数 | sync/bg | clean 机制 | 备注 |
|---|---|---|---|---|---|
| `rNET_delay` * | iface,delay_ms | duration | sync | `tc qdisc del` | v0.1 已实现示例 |
| `rNET_loss` | iface,loss_pct | duration | sync | `tc qdisc del` | `netem loss random <pct>%` |
| `rNET_reorder` | iface,reorder_pct | duration | sync | `tc qdisc del` | `netem reorder <pct>%` |
| `rNET_down` | iface | duration | sync | `ip link set up` | clean 后保证 up |
| `rNET_degrade` | iface | speed_mbps(默认10),duration | sync | `ethtool -s speed 1000` 或 autoneg | speed_mbps 可选默认 10 |
| `rNET_port_occupy` | port | protocol(默认tcp),duration | background | kill 持有进程 | 脚本前台持有 socket |
| `rNET_service_stop` | service | duration | sync | `systemctl start <service>` | safety=dangerous |
| `rNET_link_flap` | iface | cycle_sec(默认2),count(默认10) | background | kill 进程 + `ip link set up` | 不走 timeout，按 count 自结束 |
| `rNET_bw_limit` | iface,rate_kbps | duration | sync | `tc qdisc del` | `tbf rate <rate>kbps` |
| `rNET_jitter` | iface,delay_ms,jitter_ms | duration | sync | `tc qdisc del` | `netem delay <base> <jitter>` |
| `rNET_tcp_loss` | port | direction(默认both),duration | sync | `iptables -D ...` | L4 丢包；direction ∈ {in,out,both} |

#### 4.1.3 错误处理

- `tc` / `iptables` / `ethtool` / `systemctl` 不存在或无权限时，脚本非 0 退出，stderr 报错，dcat 纳入 `error.message`，退出码 1。
- `iface` 不存在时 `tc qdisc add` 报错，脚本退出非 0。
- background 故障（port_occupy / link_flap）脚本 trap SIGTERM 清理后退出。

### 4.2 进程模块（process，4 条）

#### 4.2.1 共享机制

| 机制 | 命令/工具 | 适用故障 | 备注 |
|---|---|---|---|
| `kill` 信号 | `kill -9/-STOP/-CONT <pid>` | exit / hang | exit 不可逆；hang 可逆 |
| 不可中断 IO | C helper `open + ioctl(不可中断命令)` 或 `vhangup` | dstate | 触发 D 状态，需 root |
| 僵尸生成 | fork 子进程 + 子 exit + 父不 wait | zstate | count 个僵尸 |

#### 4.2.2 4 条故障差异表

| UID | supported_ops | 必填 | 可选 | sync/bg | clean 机制 | 备注 |
|---|---|---|---|---|---|---|
| `rPROC_exit` | **inject** | pid | — | sync | **N/A** | `kill -9 <pid>`，不可恢复；**唯一 inject-only 故障** |
| `rPROC_dstate` | inject,clean,query | count | duration | background | kill D 态进程 | 脚本 spawn count 个 D 态 helper，前台驻留 |
| `rPROC_hang` | inject,clean,query | pid | duration | sync | `kill -CONT <pid>` | `kill -STOP`；clean 重跑脚本发 CONT |
| `rPROC_zstate` | inject,clean,query | count | duration | background | kill 父进程（子被 init reap） | 脚本生成 count 僵尸并前台驻留 |

#### 4.2.3 rPROC_exit 的 inject-only 语义

- `supported_ops = inject`：目录中不声明 `optional_params` / `timeout`。
- precheck 跳过"无活跃注入"检查（无 state 记录）。
- dispatch 走 inject-only 分支：`executor_run` → `output_ok(message)`，**不写 state、不启动自动清理**。
- `dcat clean rPROC_exit` / `dcat query rPROC_exit` 在 precheck 阶段拒绝（op 不在 supported_ops，退出码 3）。

### 4.3 CPU 模块（cpu，1 条 + 已有 rCPU_overload）

#### 4.3.1 共享机制

| UID | 机制 | 命令 | clean |
|---|---|---|---|
| `rCPU_overload` * | 多核 burn | `yes` 后台多实例 | kill 进程组（v0.1 已实现） |
| `rCPU_core_offline` | sysfs 离线 | `echo 0 > /sys/devices/system/cpu/cpu<N>/online` | `echo 1 > ...` |

#### 4.3.2 rCPU_core_offline 详设

- **必填**：`cores`（格式 `"0,2,4"` 或 `"0-3"`，脚本解析）。
- **可选**：`duration`（不填需手动 clean）。
- **sync** 模式：脚本 `for n in cores; do echo 0 > .../cpu<N>/online; done`，执行完返回。clean = 重跑脚本 `DCAT_OP=clean`，对所有 cores `echo 1`。
- **safety=warning**：离线核影响调度，需确认。
- **限制**：cpu0 通常不可离线（内核 CONFIG_BOOTPARAM_HOTPLUG_CPU0），脚本须跳过并 stderr 提示。

### 4.4 存储模块（storage，1 条）

#### 4.4.1 rDISK_write_overload

- **必填**：`device`（块设备路径，如 `/dev/sda` 或挂载点 `/data`）。
- **可选**：`workers`（默认 4），`duration`（不填需手动 clean）。
- **background** 模式：脚本 spawn N 个 `dd if=/dev/zero of=<device>/dcat.stress bs=1M` 或 `fio --rw=write --numjobs=N`，前台驻留。
- **clean** = `executor_kill`（kill 进程组，dd/fio 自然终止；可选 `rm -f <device>/dcat.stress`）。
- **safety=warning**：写压影响业务 IO，需确认。
- **预检**：`device` 存在且可写；`fio` 或 `dd` 可执行。

### 4.5 NPU 模块（npu，20 条，v0.3 新增）

#### 4.5.1 共享机制

所有 NPU 故障通过 `hccn_tool -i <chip> <op>` 操作 RoCE 网卡。脚本共享 `config/scripts/npu/_common.sh`（`npu_check_env`/`sidecar_save/load/clear`），每脚本内联 `fault_present` 函数实现 query-then-clean 幂等。

| clean 策略 | 适用 UID | 机制 |
|---|---|---|
| 反向操作 | arp_poison, route_add, iprule_add, iproute_add | del 加的 / add 删的 |
| sidecar 回放 | ip_change, gw_change, netdetect_change, arp_del, route_del, iprule_del, iproute_del, mtu, fec, dscp_tc, prio_tc, pfc, roce_port | inject 前 -g 存原值；clean 回放 |
| 设回 max | bw_limit | clean = -shaping -s bw_limit 100000 |
| -cfg recovery | link_down, route_clear | hccn_tool 内置恢复 |

#### 4.5.2 20 条故障差异表

（见 SPEC §3.3 完整目录表）

#### 4.5.3 约束

- 所有 NPU 故障 `sync` 模式（hccn_tool 是快命令）。
- 所有故障 `required_params` 含 `chip`（hccn_tool 的 `-i <0~7>`）。
- 4 条 dangerous（link_down/ip_change/route_clear/roce_port_change，断全 NPU 通信）；16 条 warning。
- 真实环境冒烟仅华为 Atlas 物理机可做（CI mock-only，与 v0.2 网络类一致）。

---

## 5. 关键流程

### 5.1 inject（含 inject-only 分支 + reaper 派生）

```
parse → registry_find(uid)
  ├─ NULL → output_err(4, "not found")
  └─ fault_def
     → safety_precheck(op∈supported_ops, required_params 齐全, 脚本可执行, [可恢复:无活跃])
     → safety_confirm(safety_level)
     → dispatch:
        ├─ supported_ops == inject (inject-only):
        │     executor_run(script, DCAT_OP=inject)
        │     → output_ok(message)              # 无 record_id, 无 state, 无 reaper
        └─ supported_ops == inject,clean,query (可恢复):
              ├─ background: executor_spawn(script) → state_add(uid, pid, duration)
              └─ sync:      executor_run(script) → 若 duration>0: state_add(uid, 0, duration)
              → output_ok(message, record_id)
              → 若 duration > 0:
                    dispatch_build_reaper(exe, uid, cfgpath, dur, buf, len)
                    executor_spawn(buf)         # detached reaper: sleep <dur>; dcat "clean <uid>" --yes
                                               # setsid + /dev/null, 存活到 duration 到期
```

### 5.2 clean

```
parse → registry_find(uid)
  → safety_precheck(op=clean ∈ supported_ops)        # inject-only 故障在此拒绝 (退出码 3)
  → state_find(uid)
     ├─ 无记录 → output_err(1, "no active injection")
     └─ 记录
        ├─ background: executor_kill(pid)
        └─ sync:      executor_run(script, DCAT_OP=clean, 回放原 where 参数)
        → state_mark_inactive(id) → output_ok
```

### 5.3 query / list

- **query（无 uid）**：`state_record` 遍历活跃记录，输出记录数组 JSON。不调用脚本。
- **query（有 uid）**：调脚本 `DCAT_OP=query` 分支验证故障是否真的生效。用户 `where` 参数通过环境变量传入（与 inject 参数独立）。脚本 stdout 原样输出到终端（表格/文本），dcat 打印 `---` 分隔符后输出 JSON `{"confirmed":true/false}`。inject-only 故障在 precheck 拒绝（退出码 3）。
- **list**：`registry_list()`，输出 fault 目录 JSON（含 supported_ops / required_params / optional_params / safety / exec_mode / desc）。

---

## 6. 脚本契约（实现约定）

### 6.1 通用约定

- 环境变量传参：`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_<KEY>`（KEY 大写、非字母数字→`_`）。
- 退出码 `0`=成功；非 `0`=失败。
- stdout 成功文本 → `data.message`；stderr → `error.message`。
- 可选参数未提供时，对应 `DCAT_PARAM_<KEY>` 环境变量不设置；脚本须自行处理默认值（目录表中 `(默认X)` 标注）。

### 6.2 可恢复故障脚本

- background inject 脚本前台驻留 + `trap SIGTERM` 清理子进程后退出。
- sync inject 脚本执行完返回（长任务自行 spawn + pidfile / sidecar，clean 读取清理）。
- clean 时 dcat 重跑同一脚本 `DCAT_OP=clean`，并回放原 `where` 参数（若有）。脚本退出码非 0 时 dcat 报错且**不 mark inactive**（故障可能仍在系统上）。
- query 由 dcat 调脚本 `DCAT_OP=query` 分支验证（参数为用户当前输入，非 inject 时参数）。脚本 stdout 原样输出 + `---` + JSON。
- query 由 dcat 状态回答，脚本无需实现 query。

### 6.3 inject-only 故障脚本

- 仅实现 `DCAT_OP=inject` 分支；不期望 `DCAT_OP=clean`。
- 目录中不声明 `optional_params=duration` / `timeout`。
- inject 完成即终结，不写 state。

### 6.4 默认值约定

- 目录表中 `(默认X)` 表示该可选参数缺省时的脚本内部默认值。
- 脚本须在 `DCAT_PARAM_<KEY>` 未设置时使用该默认值，**不应依赖 dcat 注入默认值到环境变量**（dcat 不解析默认）。

---

## 7. 目录结构

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
│   │   ├── safety.{c,h}
│   │   ├── state.{c,h}
│   │   ├── config.{c,h}
│   │   ├── output.{c,h}
│   │   ├── dispatch.{c,h}
│   │   └── types.h             # 公共类型: params_t/result_t/fault_def_t(含 optional_params)/injection_record_t
│   └── injectors/
│       └── injector.h          # 高级扩展点 injector_t(本期留位)
├── config/
│   ├── demoncat.conf           # 故障目录配置(38 条: v0.1 2 + v0.2 16 + v0.3 20)
│   └── scripts/
│       ├── cpu/                    # v0.1 示例 (background) + v0.2 (1 脚本)
│       │   ├── cpu_overload.sh        # v0.1 示例 (background)
│       │   └── cpu_core_offline.sh    # v0.2
│       ├── network/                # v0.1 示例 (sync) + v0.2 (10 新增脚本)
│       │   ├── net_delay.sh           # v0.1 示例 (sync)
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
│       ├── process/                # v0.2 (4 脚本)
│       │   ├── proc_exit.sh
│       │   ├── proc_dstate.sh
│       │   ├── proc_hang.sh
│       │   └── proc_zstate.sh
│       ├── storage/                # v0.2 (1 脚本)
│       │   └── disk_write_overload.sh
│       └── npu/                    # v0.3 (20 脚本 + _common.sh helper)
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
└── tests/
    ├── test_cli.c
    ├── test_registry.c
    ├── test_executor_mock.c
    ├── test_safety.c
    ├── test_state.c
    ├── test_autoclean.c
    ├── test_output.c
    ├── test_faults.c               # 通用表驱动 + duration-expiry auto-clean
    ├── test_faults_network.c      # 11 条网络故障
    ├── test_faults_process.c      # 4 条进程故障
    ├── test_faults_cpu_storage.c  # 2 条 CPU + 1 条存储故障
    ├── test_faults_npu.c          # 20 条 NPU 故障 + duration-expiry auto-clean
    └── test_reaper.c              # reaper 命令引号 + payload 可解析性
```

---

## 8. 构建

- `CMakeLists.txt`：`set(CMAKE_C_STANDARD 11)`（gnu11 扩展开启，便于 `usleep`/`select`/`fork`），静态链接，`find_package(Threads)`，把 `third_party/cjson/cJSON.c` 编进二进制，`-Wall -Wextra -Werror`。
- 目标 `dcat`；测试通过 `enable_testing()` + `add_test`，`ctest` 驱动。测试 `WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}` 以便 `config/demoncat.conf` 与 `config/scripts/*.sh` 解析。
- WSL 验证：`cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure`。

---

## 9. 测试设计

### 9.1 mock_executor

`executor_set_mock(fn)`，`fn` 捕获 `(cmd, env)` 不真正 fork。测试可断言下发命令串与环境变量集合（`DCAT_OP` / `DCAT_UID` / `DCAT_PARAM_*`）。

### 9.2 表驱动

以 `struct { input; fault_def; expect_cmd; expect_env; expect_record; expect_json; expect_exit; }` 数组驱动 inject / clean / query。

### 9.3 故障覆盖矩阵

| 故障 | inject | clean | query | mock 断言 | 真实冒烟 |
|---|:---:|:---:|:---:|---|---|
| rNET_loss / reorder / bw_limit / jitter | ✓ | ✓ | ✓ | 命令串含 `tc qdisc add ... netem/tbf`；env 含 iface/... | 可选（需 root + iface） |
| rNET_down | ✓ | ✓ | ✓ | 命令串含 `ip link set down` | 可选 |
| rNET_degrade | ✓ | ✓ | ✓ | 命令串含 `ethtool -s` | 可选 |
| rNET_port_occupy | ✓ | ✓ | ✓ | background；spawn pid；env 含 port/protocol | 可选 |
| rNET_service_stop | ✓ | ✓ | ✓ | dangerous 确认路径；命令含 `systemctl stop` | 可选 |
| rNET_link_flap | ✓ | ✓ | ✓ | background；count 自结束；不走 timeout | 可选 |
| rNET_tcp_loss | ✓ | ✓ | ✓ | 命令串含 `iptables -I ... DROP` | 可选 |
| rPROC_exit | ✓ | — | — | inject-only；output 无 record_id；clean/query 退出码 3 | 可选（kill 测试进程） |
| rPROC_dstate | ✓ | ✓ | ✓ | background；spawn helper | 可选（需 root + helper） |
| rPROC_hang | ✓ | ✓ | ✓ | `kill -STOP`；clean 发 `-CONT` | 可选 |
| rPROC_zstate | ✓ | ✓ | ✓ | background；spawn 僵尸 | 可选 |
| rCPU_core_offline | ✓ | ✓ | ✓ | 命令串含 `echo 0 > .../online` | 可选（需 root） |
| rDISK_write_overload | ✓ | ✓ | ✓ | background；fio/dd；clean kill 进程组 | 可选 |
| 全部 20 条 rNPU_* | ✓ | ✓ | ✓ | 命令串含 npu/<script>.sh；env 含 chip + 各故障参数 | 不做（仅 Atlas 物理机有 hccn_tool） |

### 9.4 真实环境冒烟

- 可恢复故障：inject → query(active) → 等 duration 到期 → lazy_clean → query(empty) → 无残留进程。
- inject-only：inject → query(无记录) → clean 拒绝（退出码 3）。
- 手动 clean：inject（不填 duration）→ query(active) → clean → query(empty)。
- dangerous 确认：`n`→aborted 退出码 3；`yes`→proceed 退出码 0。

---

## 10. 命令行与使用场景

### 10.1 命令结构

```
dcat "<command> <uid> [params]" [--config <path>] [--yes] [--help]
```

单条命令串（quoted），不是子命令模式。

### 10.2 全局参数

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--config <path>` | 固定路径见 SPEC §7 | 配置文件路径覆盖 |
| `--yes` | false | 跳过 warning 级确认（不跳过 dangerous） |
| `--help` | — | 显示帮助 |

### 10.3 使用场景

#### 场景一：可恢复故障注入（带自动恢复）

```bash
# 注入 CPU 过载 4 核 60 秒，自动恢复
dcat "inject rCPU_overload (cores,duration) values (4,60)" --yes

# 注入网络丢包 5% 30 秒
dcat "inject rNET_loss (iface,loss_pct,duration) values (eth0,5,30)" --yes

# 查询活跃
dcat query
```

#### 场景二：可恢复故障注入（不自动恢复，手动 clean）

```bash
# 注入网络丢包，不填 duration → 不自动恢复
dcat "inject rNET_loss (iface,loss_pct) values (eth0,5)" --yes

# 手动 clean
dcat "clean rNET_loss where iface=eth0"
```

#### 场景三：一次性故障（inject-only）

```bash
# 进程异常退出（不可恢复，无 duration / 无 clean）
dcat "inject rPROC_exit (pid) values (12345)"

# query 看不到记录
dcat query

# clean 被拒绝（退出码 3）
dcat "clean rPROC_exit"
```

#### 场景四：查询与列表

```bash
dcat "query rCPU_overload"             # 单 uid 活跃记录
dcat query                              # 全部活跃记录
dcat list                               # 故障目录
```

---

## 11. 与 SPEC 的对应

- **决策1（注册机制）**：registry 从 cnf 载入 `fault_def`（含 v0.2 新增 `optional_params`）；编译 `injector_t` 仅留高级扩展位（§3.3 + §11）。
- **决策2（where 子句）**：双语法统一进 `params_t`（§3.2）。
- **决策3（故障机制）**：cnf + 脚本驱动（§1.2 + §4 + §6）。
- **决策4（参数传递）**：环境变量，不走 argv（§3.4 + §6）。
- **决策5（必/选参数区分，v0.2 新增）**：`required_params` 预检校验；`optional_params` 缺省走脚本默认；`duration` 一律可选（§2 + §3.3 SPEC）。
- **决策6（inject-only 故障，v0.2 新增）**：`supported_ops=inject` 的一次性故障不建 state、无 clean/query/timeout；dispatch 走 inject-only 分支（§3.9 + §5.1 + §4.2.3）。
- **决策7（发布批次，v0.2 修订）**：放弃按模块分 P1/P2/P3，改 v0.1 / v0.2 / v0.3+ 批次式持续扩充（SPEC §8）。
- **决策8（Reaper 子进程，v0.2 修复）**：one-shot CLI 的 inject-with-duration 场景，dcat 退出后后台自动清理线程随之死亡，故障会持续到用户下次跑 dcat 才被 `state_lazy_clean` 清理。修复：inject 成功且 `duration > 0` 时 spawn detached reaper `sleep <dur>; dcat "clean <uid>" --yes`（setsid + /dev/null），到期自己醒来 clean。reaper 是自动恢复的主路径；进程内线程/惰性清理为兜底（SPEC §4.3 + §3.1 + §5.1）。
- **决策9（配置定位，v0.2 修订）**：放弃三级 `find_config()` 搜索（CWD/home/etc），改为二进制相对固定路径 `<binary_dir>/../config/demoncat.conf`（通过 `/proc/self/exe` 解析）。conf 里的相对脚本路径在 `config_load` 时通过 `derive_project_root` + `resolve_script` 自动补成绝对路径，dcat 可从任意 CWD 运行（SPEC §7.1 + §3.7）。
