# DemonCAT 技术规格说明书 (SPEC)

> **DemonCAT**（简称 **dcat**）— Demon Computing Availability Tools
> Linux 计算故障注入 CLI 编排器：统一的命令面、安全护栏、状态跟踪与超时自动恢复；具体故障以**外部脚本 + 声明式配置**接入。

---

## 1. 概述

### 1.1 软件定位

DemonCAT 是面向计算系统（CPU / 内存 / 存储 / 网络 / 进程 / NPU）的**故障注入编排器**。其核心价值不是实现每一种故障原语，而是提供**统一命令面、安全护栏、状态跟踪与超时自动恢复**，把具体故障以**外部脚本 + 声明式配置**的方式接入。加一个故障 = 加一个脚本 + 配置文件一行，**免重新编译**。

### 1.2 技术栈

| 项目 | 选型 |
|------|------|
| 开发语言 | C11（ISO/IEC 9899:2011） |
| 构建 | CMake ≥ 3.10 |
| 目标平台 | Linux（glibc / musl），WSL 兼容 |
| 第三方依赖 | 仅 cJSON 单文件库（vendored 进仓库 `third_party/cjson/`） |
| 链接 | 目标二进制静态链接 |
| 线程 | pthread（状态锁 + 后台自动清理线程） |
| 配置文件 | INI（`demoncat.conf`） |
| 数据输出 | stdout JSON |

### 1.3 核心需求

1. **数据驱动扩展**：新增故障只需加脚本 + 配置文件一段，**免重新编译**。
2. **框架稳定、故障可变**：解析 / 安全 / 预检 / 状态 / 超时清理 / JSON 输出等横切逻辑编译进二进制并保持稳定；per-fault 部分以脚本表达。
3. **可测**：通过 `mock_executor` 捕获实际下发的命令串与环境变量，做表驱动断言。
4. **开闭原则**：对扩展开放（新故障），对修改关闭（不动二进制）。
5. **安全护栏**：注入前预检 + 分级确认（normal / warning / dangerous）+ 超时自动恢复，防止误操作与故障泄漏。
6. **可恢复与一次性区分**：可恢复故障带 `clean` op + state 记录 + 可选 `timeout` 自动恢复；一次性故障 `inject`-only，无 state、无 clean。

### 1.4 设计原则

- **核心路径零动态分配**：解析 / 查表 / 分发均栈上分配；仅输出序列化（cJSON）在边界堆分配。
- **参数走环境变量不走 argv**：免 shell 注入、语言无关。
- **状态即真源**：`query` 由 dcat 自身 state 回答，不调用脚本。

---

## 2. 统一命令格式

```
dcat "<command> <uid> [(param1,param2,...) values (value1,value2,...) | where k1=v1 k2=v2 ...]"
```

- `command` ∈ `{ inject, clean, query, list }`
- `uid`：故障唯一标识（如 `rCPU_overload`），在配置中声明
- `inject` 用 `(p1,p2) values (v1,v2)` 传参
- `clean` / `query` 用 `where k1=v1 k2=v2` 传参（可省略，表示作用于该 uid 全部活跃记录）
- 两种参数语法在内部统一解析为同一个 `params_t` 结构

### 2.1 命令集

| 命令 | 语义 | 适用 supported_ops |
|---|---|---|
| `inject <uid> (p1,p2) values (v1,v2)` | 注入指定故障，按参数配置；危险级需二次确认 | `inject` 或 `inject,clean,query` |
| `clean <uid> [where k=v ...]` | 清除该 uid（或匹配条件的）活跃注入；超时故障由后台线程自动调用 | 仅 `inject,clean,query` |
| `query [uid] [where k=v ...]` | 无 uid：查询 dcat 自身活跃注入记录；有 uid：调脚本 `query` 分支验证故障是否真的在系统上生效，用户参数与 inject 参数独立 | `inject,clean,query` |
| `list` | 列出配置中声明的全部故障目录 | 所有 |

> `inject`-only 故障（如 `rPROC_exit`）不支持 `clean` / `query`；注入即终结，无活跃记录。

### 2.2 示例

```
# 可恢复故障：带 duration 触发自动恢复
dcat "inject rCPU_overload (cores,duration) values (4,60)"
dcat "inject rNET_delay (iface,delay_ms,duration) values (eth0,100,30)"

# 可恢复故障：不填 duration，不自动恢复，需手动 clean
dcat "inject rNET_loss (iface,loss_pct) values (eth0,5)"
dcat "clean rNET_loss where iface=eth0"

# 一次性故障：无 duration / 无 clean
dcat "inject rPROC_exit (pid) values (12345)"

# 查询与列表
dcat "query rCPU_overload"
dcat "query"
dcat "list"
```

---

## 3. 故障目录

### 3.1 目录字段语义

每个故障在 `demoncat.conf` 中以 `[fault.<uid>]` 段声明，字段如下：

| 字段 | 必填 | 说明 |
|---|---|---|
| `module` | 是 | 模块归类：`cpu` / `memory` / `storage` / `network` / `process` / `npu` |
| `desc` | 否 | 一句话描述 |
| `script` | 是 | 外部脚本路径（绝对或相对；相对路径基于项目根目录自动解析为绝对），须可执行 |
| `supported_ops` | 是 | 支持的操作子集，逗号分隔：`inject` 或 `inject,clean,query` |
| `required_params` | 否 | **必填**参数名（逗号分隔），预检校验完整性，缺失即拒 |
| `optional_params` | 否 | **可选**参数名（逗号分隔），不填时脚本走自有默认值 |
| `safety` | 是 | `normal` / `warning` / `dangerous` |
| `exec_mode` | 是 | `sync`（同步执行）/ `background`（dcat 后台托管 pid + clean 时 kill） |
| `timeout` | 否 | 目录级 hint（v0.2 起实际由 inject 时的 `duration` 参数驱动，见 §3.2）；**仅 `inject,clean,query` 故障有意义**；`inject`-only 故障不应声明 |

### 3.2 参数与 timeout 语义约定

- **必填 vs 可选**：`required_params` 在 precheck 阶段校验非空；`optional_params` 缺省时不报错，由脚本解释默认值（目录表中以 `(默认X)` 标注）。
- **`duration` 一律可选**：`duration` 是可选参数，语义为"注入存活秒数"，是**自动恢复的唯一驱动**。
  - 提供 `duration` → dcat 在 state 中记 `expires_at = now + duration`；到期后台线程自动 clean。
  - 不提供 `duration` → 无 `expires_at`，需手动 `clean`（对 `inject`-only 故障则无意义）。
  - **特例**：少数故障不接受 `duration`（如 `rNET_link_flap` 按 `count` 自结束），目录表 `optional_params` 不含 `duration`；其恢复走手动 `clean` 或脚本自然退出。
- **`inject`-only 故障无 timeout 语义**：一次性、不可恢复，目录中 `timeout` 字段标注 `N/A`。

### 3.3 目录清单（共 38 条：v0.1 已有 2 + v0.2 新增 16 + v0.3 新增 20）

> 标 `*` 为 v0.1 已实现示例；其余 16 条为 v0.2 新增；末 20 条 `rNPU_*` 为 v0.3 新增（需求"网络包延时"对应 `rNET_delay`，已在 v0.1 实现）。完整 cnf 声明见 §7。

| UID | module | supported_ops | required_params | optional_params | safety | exec_mode | timeout 语义 |
|---|---|---|---|---|---|---|---|
| `rCPU_overload` * | cpu | inject,clean,query | cores | duration | warning | background | =duration(可省) |
| `rNET_delay` * | network | inject,clean,query | iface,delay_ms | duration | normal | sync | =duration(可省) |
| `rNET_loss` | network | inject,clean,query | iface,loss_pct | duration | normal | sync | =duration(可省) |
| `rNET_reorder` | network | inject,clean,query | iface,reorder_pct | duration | normal | sync | =duration(可省) |
| `rNET_down` | network | inject,clean,query | iface | duration | warning | sync | =duration(可省) |
| `rNET_degrade` | network | inject,clean,query | iface | speed_mbps(默认10),duration | warning | sync | =duration(可省) |
| `rNET_port_occupy` | network | inject,clean,query | port | protocol(默认tcp),duration | warning | background | =duration(可省) |
| `rNET_service_stop` | network | inject,clean,query | service | duration | dangerous | sync | =duration(可省) |
| `rNET_link_flap` | network | inject,clean,query | iface | cycle_sec(默认2),count(默认10) | warning | background | 不走 timeout(按 count 自结束) |
| `rNET_bw_limit` | network | inject,clean,query | iface,rate_kbps | duration | normal | sync | =duration(可省) |
| `rNET_jitter` | network | inject,clean,query | iface,delay_ms,jitter_ms | duration | normal | sync | =duration(可省) |
| `rNET_tcp_loss` | network | inject,clean,query | port | direction(默认both),duration | warning | sync | =duration(可省) |
| `rPROC_exit` | process | **inject** | pid | — | dangerous | sync | **N/A（不可恢复）** |
| `rPROC_dstate` | process | inject,clean,query | count | duration | warning | background | =duration(可省) |
| `rPROC_hang` | process | inject,clean,query | pid | duration | warning | sync | =duration(可省) |
| `rPROC_zstate` | process | inject,clean,query | count | duration | normal | background | =duration(可省) |
| `rCPU_core_offline` | cpu | inject,clean,query | cores | duration | warning | sync | =duration(可省) |
| `rDISK_write_overload` | storage | inject,clean,query | device | workers(默认4),duration | warning | background | =duration(可省) |
| `rNPU_link_down` | npu | inject,clean,query | chip | duration | dangerous | sync | =duration(可省) |
| `rNPU_ip_change` | npu | inject,clean,query | chip,address,netmask | duration | dangerous | sync | =duration(可省) |
| `rNPU_gw_change` | npu | inject,clean,query | chip,gateway | duration | warning | sync | =duration(可省) |
| `rNPU_netdetect_change` | npu | inject,clean,query | chip,address | duration | warning | sync | =duration(可省) |
| `rNPU_arp_poison` | npu | inject,clean,query | chip,dev,ip,mac | duration | warning | sync | =duration(可省) |
| `rNPU_arp_del` | npu | inject,clean,query | chip,dev,ip | duration | warning | sync | =duration(可省) |
| `rNPU_route_add` | npu | inject,clean,query | chip,address,netmask,gateway | duration | warning | sync | =duration(可省) |
| `rNPU_route_del` | npu | inject,clean,query | chip,address,netmask | duration | warning | sync | =duration(可省) |
| `rNPU_route_clear` | npu | inject,clean,query | chip | duration | dangerous | sync | =duration(可省) |
| `rNPU_iprule_add` | npu | inject,clean,query | chip,dir,ip,table | duration | warning | sync | =duration(可省) |
| `rNPU_iprule_del` | npu | inject,clean,query | chip,dir,ip | duration | warning | sync | =duration(可省) |
| `rNPU_iproute_add` | npu | inject,clean,query | chip,ip,ip_mask,via,dev,table | duration | warning | sync | =duration(可省) |
| `rNPU_iproute_del` | npu | inject,clean,query | chip,ip,ip_mask,table | duration | warning | sync | =duration(可省) |
| `rNPU_bw_limit` | npu | inject,clean,query | chip,bw_limit | duration | warning | sync | =duration(可省) |
| `rNPU_mtu_mismatch` | npu | inject,clean,query | chip,size | duration | warning | sync | =duration(可省) |
| `rNPU_fec_change` | npu | inject,clean,query | chip,encoding | duration | warning | sync | =duration(可省) |
| `rNPU_dscp_tc_change` | npu | inject,clean,query | chip,dscp,tc | duration | warning | sync | =duration(可省) |
| `rNPU_prio_tc_change` | npu | inject,clean,query | chip,map | duration | warning | sync | =duration(可省) |
| `rNPU_pfc_change` | npu | inject,clean,query | chip,bitmap | duration | warning | sync | =duration(可省) |
| `rNPU_roce_port_change` | npu | inject,clean,query | chip,port | duration | dangerous | sync | =duration(可省) |

### 3.4 扩展约定

- **新增模块**：在 `module` 字段取新值（如 `memory`），脚本放到 `config/scripts/<module>/`，cnf 加段即可。`npu` 模块已在 v0.3 落地（见 §3.3 末 20 条 `rNPU_*`）。
- **现有模块加故障**：同模块目录加脚本 + cnf 加段，UID 不重复即可。
- 目录将持续扩充（预计 200+），不预设模块实现先后顺序，按发布批次推进（见 §8）。

---

## 4. 安全与预检

### 4.1 安全等级

| 等级 | 确认行为 | 退出码（拒绝时） |
|---|---|---|
| `normal` | 直接执行 | — |
| `warning` | 提示风险，默认 **N**，需输入 `y` 继续；`--yes` 可跳过 | 3 |
| `dangerous` | 提示风险，需输入 **yes**（全小写）继续；非交互场景（无 tty）直接拒绝；`--yes` **不跳过** dangerous | 3 |

### 4.2 inject 前预检（Precheck）

按以下顺序校验，任一失败即中止并返回错误 JSON（退出码 3 或对应码）：

1. uid 在配置中存在（否则退出码 4）
2. 请求的 op 属于该故障 `supported_ops`（`inject`-only 故障拒绝 `clean`/`query`）
3. `inject`：`required_params` 全部提供且非空
4. 脚本路径存在且可执行（`access/X_OK`）
5. **仅 `inject,clean,query` 故障**：该 uid 当前无活跃注入（同 uid 不允许并发，除非 clean 后再 inject）

> `inject`-only 故障跳过第 5 步（不建 state 记录，无所谓并发）。

### 4.3 超时与自动清理

- **仅 `inject,clean,query` 故障 + 提供了 `duration`** 才进入自动恢复流程：dcat 在 state 中记录 `expires_at = now + duration`。
- **自动恢复有两条路径，互补确保 duration 到期必 clean**：

  **路径一：Reaper 子进程（主路径）**。inject 成功且 `duration > 0` 时，dcat 通过 `executor_spawn` 派生一个 **detached** 子进程：
  ```
  sleep <duration>; <dcat路径> "clean <uid>" --config <cfg> --yes
  ```
  该子进程 `setsid()` 脫离终端会话、stdio 重定向到 `/dev/null`，dcat 退出后继续存活。到期后 reaper 自己醒来执行 `dcat clean`，完成故障恢复。**用户注入带 duration 后即可离开，无需再跑 dcat 命令。**

  > 为什么需要 reaper：dcat 是 one-shot CLI，inject 后进程就退出了；进程内的后台自动清理线程随之死亡。没有 reaper 的话，故障会一直持续到用户碰巧再跑一次 dcat（此时 `state_lazy_clean` 才会清理过期记录）——违反"duration 到期自动恢复"契约。

  **路径二：进程内自动清理（辅助路径）**。dcat 运行期间，后台线程每秒扫描 state，对到期记录调用 clean；dcat 启动时还会同步执行 `state_lazy_clean()` 清理上次退出后过期的记录。用于 dcat 长运行场景与 reaper 失效时的兜底。

- **两条路径的关系**：reaper 是 one-shot CLI 场景的主路径（dcat 退出后靠它）；进程内线程/惰性清理用于 dcat 长运行场景。reaper 启动的 `dcat clean` 进程在开头也会执行 `state_lazy_clean()`，因此即使 reaper 的 `clean` 命令因记录已被 `state_lazy_clean` 标记 inactive 而返回"no active injection"，实际清理仍由 `state_lazy_clean` 完成——两条路径互为兜底，不会互相冲突。
- `background` 模式 clean = `executor_kill`（先 SIGTERM 后 SIGKILL，整进程组）。
- `sync` 模式 clean = 重跑脚本 `DCAT_OP=clean`（带原 `where` 参数），由脚本负责撤销。
- `inject`-only 故障 / 未提供 `duration` 的可恢复故障 → 不进入自动恢复，不 spawn reaper。

---

## 5. 脚本契约

dcat 通过环境变量向脚本传递操作与参数（免 shell 注入、语言无关）：

| 环境变量 | 含义 |
|---|---|
| `DCAT_OP` | `inject` / `clean` / `query` |
| `DCAT_UID` | 故障 uid |
| `DCAT_PARAM_<KEY>` | 每个参数，KEY 为参数名大写、非字母数字字符替换为 `_` |

### 5.1 通用约定

- **退出码**：`0` 成功；非 0 失败。
- **stdout**：成功时可选输出一行说明文本，dcat 纳入结果 JSON 的 `data.message`。
- **stderr**：失败时输出错误信息，dcat 纳入 `error.message`。
- 可选参数未提供时，对应 `DCAT_PARAM_<KEY>` 环境变量不设置；脚本须自行处理默认值（目录表中以 `(默认X)` 标注期望默认）。

### 5.2 可恢复故障（`inject,clean,query`）

- **`background` 模式**：inject 脚本须**前台驻留**（dcat 用 `executor_spawn` 托管其 pid）；脚本应 `trap SIGTERM` 清理自身子进程后退出。clean = dcat 主动 kill 托管 pid。
- **`sync` 模式**：inject 脚本执行完即返回（可自行 spawn-and-exit 长任务并写 sidecar pidfile，clean 时读取清理）。clean = dcat 重跑脚本 `DCAT_OP=clean`，并回放原 `where` 参数（若有）。
- **`query`**：由 dcat 自身 state 回答，**脚本无需实现 query**。

### 5.3 一次性故障（`inject`-only）

- 无 `clean` op、无 `query` op、无 `duration`、无 state 记录、无自动恢复。
- inject 脚本执行完（sync）或被 dcat 托管至自然退出（background，本期 v0.2 无此类组合）即终结。
- 典型：`rPROC_exit`（`kill -9`，进程已死不可逆）。
- dcat 在 `inject` 成功后直接 `output_ok`，不写 state、不启动自动清理。
- `dcat "query rPROC_exit"` 在 precheck 阶段拒绝（query 不在 supported_ops，退出码 3）。

### 5.4 query 分支（故障验证）

可恢复故障脚本（`supported_ops` 含 `query`）须实现 `DCAT_OP=query` 分支：

- dcat 通过环境变量传入**用户当前输入的参数**（`DCAT_PARAM_*`），**不是** inject 时的参数——用户可注入 CPU1 满载后查询 CPU2 的负载。
- 脚本检查**实际系统状态**（如 `top`/`tc qdisc show`/`pgrep`/`sysfs`），输出任意格式的证据文本（表格、多行文本）到 stdout。
- **退出码**：`0` = 故障确认生效 / 非 `0` = 未生效。
- **输出格式**（方案 A）：dcat 原样输出脚本 stdout，然后打印 `---` 分隔符，最后输出 JSON：
  ```
  <脚本原始输出（表格/文本）>
  ---
  {"status":"ok","op":"query","uid":"rCPU_overload","data":{"confirmed":true}}
  ```

---

## 6. 输出格式

统一向 stdout 输出 JSON：

**成功（可恢复 inject）**：
```json
{"status":"ok","op":"inject","uid":"rCPU_overload","data":{"message":"...","record_id":3}}
```

**成功（inject-only）**：
```json
{"status":"ok","op":"inject","uid":"rPROC_exit","data":{"message":"killed pid 12345"}}
```
> inject-only 不返回 `record_id`（无 state 记录）。

**失败**：
```json
{"status":"error","op":"inject","uid":"rCPU_overload","error":{"code":5,"message":"already active"}}
```

**`query`（无 uid，state 查询）**：
```json
{"status":"ok","op":"query","data":[{"uid":"rCPU_overload","record_id":3,"started_at":1721000000,"expires_at":1721000060,"active":true}]}
```

**`query`（有 uid，故障验证 — 方案 A 输出）**：
```
yes_processes: 2
--- cpu usage ---
%Cpu(s): 98.0 us, 1.0 sy, 0.0 ni, 0.0 id
---
{"status":"ok","op":"query","uid":"rCPU_overload","data":{"confirmed":true}}
```
> 脚本原始输出在前，`---` 分隔，JSON 在后。`confirmed: true` = 故障确认生效。

**`list`**：
```json
{"status":"ok","op":"list","data":[{"uid":"rCPU_overload","module":"cpu","safety":"warning","supported_ops":["inject","clean","query"],"desc":"..."}]}
```

### 6.1 退出码

| 退出码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 运行错误（脚本非 0 退出、fork/exec 失败等） |
| 2 | 解析错误（命令格式不合法） |
| 3 | 预检 / 安全拒绝 |
| 4 | 未找到（uid 不在目录中） |

---

## 7. 配置文件

`demoncat.conf`（INI 格式）同时承载运行时配置与故障目录：

```ini
[demoncat]
state_file = ~/.demoncat/state.json      ; 状态持久化路径
log_level  = warn                          ; debug | info | warn | error

[fault.rNET_loss]
module          = network
desc            = 网络丢包（tc netem loss）
script          = /usr/lib/demoncat/scripts/network/net_loss.sh
supported_ops   = inject,clean,query
required_params = iface,loss_pct
optional_params = duration
safety          = normal
exec_mode       = sync
timeout         = 0                        ; 0=不自动到期；实际生效值取 inject 时的 duration 参数

[fault.rPROC_exit]
module          = process
desc            = 进程异常退出（kill -9，不可恢复）
script          = /usr/lib/demoncat/scripts/process/proc_exit.sh
supported_ops   = inject
required_params = pid
safety          = dangerous
exec_mode       = sync
; inject-only 故障不写 timeout / optional_params
```

### 7.1 配置定位

`dcat` 通过 `/proc/self/exe` 解析自身路径，推导出**固定相对路径**：

```
<binary_dir>/../config/demoncat.conf
```

例如 `dcat` 位于 `/opt/dcat/build/dcat` 时，配置固定在 `/opt/dcat/config/demoncat.conf`。配置、脚本目录与二进制一同部署，无需环境变量、无需拷贝。`--config <path>` 可覆盖此默认值（测试/特殊场景）。

### 7.2 timeout 字段与 duration 参数的关系

- **`duration` inject 参数是自动恢复的唯一驱动**（v0.2 模型，对齐 §3.2）：
  - `duration` 提供 → `expires_at = now + duration`，到期自动 clean。
  - `duration` 缺省 → 不自动恢复，需手动 `clean`。
- 目录级 `timeout` 字段在 v0.2 中退化为 **hint**（保留以兼容 v0.1 配置；v0.2 实际生效值取 inject 时的 `duration` 参数）。建议新故障目录中 `timeout` 留 `0` 或省略。
- `inject`-only 故障目录中不应声明 `timeout` / `optional_params=duration`（语义上无意义）。

---

## 8. 发布批次

DemonCAT 故障总量预计 200+，按需求增量推进，**不按模块预设先后顺序**。新增模块（如 `memory` / `npu`）或在现有模块内加故障均属正常扩充。

| 批次 | 范围 | 状态 |
|---|---|---|
| **v0.1** | 核心框架 + 2 个示例故障（`rCPU_overload` / `rNET_delay`）+ 测试 | ✅ 已完成 |
| **v0.2** | 需求.md 17 项：network 11（含 rNET_delay 已 v0.1 实现）+ process 4 + cpu 1 + storage 1；新增 16 UID | ✅ 已完成 |
| **v0.3** | npu 模块 20 新故障（hccn_tool：连通性9+策略路由4+性能6+协议1） | ✅ 已完成 |
| **v0.4+** | 持续按需扩充，不预设模块顺序；可新增模块（memory / …）或在现有模块内加故障 | 📋 后续 |

每批次的实现内容 = `config/scripts/` 加脚本 + `demoncat.conf` 加段 + `tests/test_faults.c` 加表驱动用例；**不修改二进制核心**（开闭原则）。

---

## 9. 测试策略

### 9.1 测试流程要求

1. **每增加一个故障，必须验证 inject / clean / query 三路径**（`inject`-only 故障仅验证 inject）。测试不通过则修改脚本/配置重新测试，直到通过。
2. **每完成一个发布批次，做一次完整测试**，ctest 全绿。
3. 测试过程中遇到的问题自行解决，不依赖外部协助。

### 9.2 测试覆盖范围

| 层级 | 范围 | 工具 | 测试文件 |
|---|---|---|---|
| 单元测试 | cli 解析、registry 查找、safety 预检全路径、state 记录/到期 | CTest + mock_executor | test_cli / test_registry / test_safety / test_state |
| 执行器 mock | executor_run/spawn/kill 的 mock 钩子 | CTest | test_executor_mock |
| 输出格式 | result_t 构建/打印/释放 | CTest | test_output |
| 自动清理 | state_lazy_clean + 后台线程到期触发 | CTest | test_autoclean |
| 表驱动故障 | 38 故障的 inject/clean/query 下发命令串 + env | CTest + mock_executor | test_faults（通用）+ test_faults_network / test_faults_process / test_faults_cpu_storage / test_faults_npu（按模块） |
| Reaper 命令 | reaper 命令引号正确性 + 引号内 payload 可被 cli_parse 解析 | CTest | test_reaper |
| 真实脚本测试 | 2 个示例故障用 mock（不断言真 CPU / 真 tc） | CTest | 同上 |
| 超时自动清理 | state_lazy_clean + 后台线程到期触发 | CTest | test_autoclean |
| 端到端冒烟 | 真实 dcat 二进制：inject→query→自动到期→query→无残留 | 手工冒烟 | — |

### 9.3 mock_executor

`executor_set_mock(fn)`：`fn` 捕获 `(cmd, env)` 不真正 fork，返回伪造 `result_t`。测试可断言下发命令串与环境变量集合，避免真硬件依赖。

---

## 10. 非功能性需求

| 项目 | 要求 |
|---|---|
| 优雅退出 | 捕获 SIGINT/SIGTERM，等待当前 op 完成；state 持久化后退出 |
| 日志 | stderr 输出；级别由 `log_level` 控制（`debug/info/warn/error`）；生产默认 `warn` |
| 错误隔离 | 单个故障 inject/clean 失败不影响 dcat 主流程与其他故障 |
| 资源占用 | 静态二进制；核心路径零动态分配；后台线程仅 long-run 场景活跃 |
| 跨平台 | Linux（glibc/musl）；WSL 兼容；不要求 Windows |
| 可测 | mock_executor + 表驱动；无硬件可测全部 38 故障的下发命令串 |
| 状态持久化 | state 变更后写 `~/.demoncat/state.json`（cJSON 序列化），启动加载恢复 record_id 计数与未到期记录 |

---

## 11. 高级扩展点（本期不实现，仅留位）

少数需要进程内自定义逻辑（精确定时、二进制协议）的故障可走**编译注入器**路径：实现 `injector_t`（`uid` + 4 个函数指针 `inject/clean/query/precheck`）并注册到 `builtin_injectors[]`。registry 查找时 cnf 故障优先，未命中再查编译注入器。

> 本期 YAGNI，仅文档与头文件 `src/injectors/injector.h` 留位。所有故障均走 cnf + 脚本路径。
