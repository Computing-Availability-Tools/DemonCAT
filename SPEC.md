# DemonCAT 技术规格说明书 (SPEC)

> **DemonCAT**（简称 **dcat**）— Demon Computing Availability Tools
> Linux 计算故障注入工具：统一的命令面、预检护栏、状态跟踪；具体故障以**外部脚本 + 声明式配置**接入。

---

## 1. 概述

### 1.1 软件定位

DemonCAT 是面向计算系统（CPU / 内存 / 存储 / 网络 / 进程 / NPU / Docker / 文件系统 / 系统）的**故障注入工具**。其核心价值不是实现每一种故障原语，而是提供**统一命令面、状态跟踪**，把具体故障以**外部脚本 + 声明式配置**的方式接入。加一个故障 = 加一个脚本 + 配置文件一行，**免重新编译**。

### 1.2 技术栈

| 项目 | 选型 |
| ------ | ------ |
| 开发语言 | C11（ISO/IEC 9899:2011） |
| 构建 | CMake ≥ 3.10 |
| 目标平台 | Linux（glibc / musl），WSL 兼容 |
| 第三方依赖 | 仅 cJSON 单文件库（vendored 进仓库 `third_party/cjson/`） |
| 线程 | pthread（状态锁） |
| 配置文件 | INI（`demoncat.conf`） |
| 数据输出 | stdout JSON（list 为文本表格） |

### 1.3 核心需求

1. **数据驱动扩展**：新增故障只需加脚本 + 配置文件一段，**免重新编译**。
2. **框架稳定、故障可变**：解析 / 预检 / 状态 / JSON 输出等横切逻辑编译进二进制并保持稳定；
3. **命令一致性**：所有故障支持 inject(注入)、clean(清除)、query(查询) 三个操作；造成机器等不可恢复的故障（如进程退出）除外，仅支持 inject；
4. **故障隔离**：单个故障注入不影响其他操作；
5. **开闭原则**：对扩展开放（新故障），对修改关闭（不动二进制）；
6. **预检护栏**：注入前预检，校验参数完整性与脚本可执行性；
7. **可追溯**：每次注入记录日志，支持查询注入状态；
8. **可测**：通过 `mock_executor` 捕获实际下发的命令串与环境变量，做表驱动断言。
9. **TDD 驱动开发**：每个模块先编写测试用例定义期望行为，再实现功能代码使测试通过；测试用例是行为的权威定义。

### 1.4 设计原则

- **核心路径零动态分配**：解析 / 查表 / 分发均栈上分配；仅输出序列化（cJSON）在边界堆分配。
- **参数走环境变量不走 argv**：免 shell 注入、语言无关。
- **状态即真源**：`query`（无 uid）由 dcat 自身 state 回答，不调用脚本。
- **同步阻塞执行**：所有故障 inject/clean/query 均**同步阻塞**调用脚本，执行完返回。需要长驻的故障由脚本自行 spawn 子进程并写 pidfile/sidecar，clean 时重跑脚本读取清理。

---

## 2. 统一命令格式

```bash
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

- `subcommand` ∈ `{ inject, clean, query, list, serve }`
- `uid`：故障唯一标识（如 `rCPU_overload`），在配置中声明；`query` 和 `list` 可省略 uid
- 所有参数以 `--key=value` 标志传入，可选参数不填即省略对应标志
- 全局选项 `--config` / `--help` 与参数标志混合使用

### 2.1 命令集

| 命令 | 语义 | 适用 supported_ops |
| --- | --- | --- |
| `inject <uid> --p1=v1 --p2=v2 ...` | 注入指定故障，按参数配置；**同步阻塞执行脚本，执行完返回** | `inject` 或 `inject,clean,query` |
| `clean <uid> [--k1=v1 ...]` | 清除该 uid 活跃注入。**带参数**：按参数匹配 state 记录逐条清理；**无参数**：stateless 模式，脚本自行 glob `/tmp` 工件（pidfile/sidecar）清理该 uid 全部注入，不依赖 state.json | 仅 `inject,clean,query` |
| `clean --all` | 对全部支持 clean 的故障 fan-out 无参 clean（stateless，不依赖 state.json）；聚合每 uid 结果。state.json 丢失/损坏时仍可清 | — |
| `query [uid] [--k1=v1 ...]` | 无 uid：查询 dcat 自身全部活跃注入记录；有 uid：调脚本 `query` 分支验证故障是否真的在系统上生效，用户参数与 inject 参数独立 | `inject,clean,query` |
| `list` | 列出配置中声明的全部故障目录 | 所有 |
| `serve [--port N] [--bind ADDR] [--webroot DIR] [--allow-write]` | 启动 HTTP 控制平面（长驻）：内置静态前端 + `/api/*` 端点；默认只读，`--allow-write` 开注入/清理 | — |

> `inject`-only 故障（如 `rPROC_exit`）不支持 `clean` / `query`；注入即终结，无活跃记录。
> 本期不实现超时自动恢复；所有可恢复故障注入后需用户手动 `clean`。

### 2.2 示例

```bash
# 可恢复故障：需手动 clean
dcat inject rNET_loss --iface=eth0 --loss_pct=5
dcat clean rNET_loss --iface=eth0

# 并发注入同 uid 不同参数
dcat inject rNET_loss --iface=eth0 --loss_pct=5
dcat inject rNET_loss --iface=eth1 --loss_pct=3
dcat clean rNET_loss --iface=eth0    # 只清 eth0

# 同资源重注入：默认拒绝；--force 原子替换
dcat inject rNET_loss --iface=eth0 --loss_pct=5
dcat inject rNET_loss --iface=eth0 --loss_pct=8             # REJECT (同 iface, code 5)
dcat inject rNET_loss --iface=eth0 --loss_pct=8 --force     # 原子替换

# 一次性故障：无法 clean
dcat inject rPROC_exit --pid=12345

# 查询与列表
dcat query rCPU_overload
dcat query
dcat list
```

---

## 3. 故障目录

### 3.1 目录字段语义

每个故障在 `demoncat.conf` 中以 `[fault.<uid>]` 段声明，字段如下：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `module` | 是 | 模块归类：`cpu` / `memory` / `storage` / `network` / `process` / `npu` / `docker` / `filesystem` / `system` |
| `desc` | 否 | 一句话描述 |
| `script` | 是 | 外部脚本路径（绝对或相对；相对路径基于项目根目录自动解析为绝对），须可执行 |
| `supported_ops` | 是 | 支持的操作子集，逗号分隔：`inject` 或 `inject,clean,query` |
| `inject_required` | 否 | inject 操作**必填**参数名（逗号分隔），预检校验完整性，缺失即拒；无则省略 |
| `inject_optional` | 否 | inject 操作**可选**参数名（逗号分隔），不填时脚本走自有默认值 |
| `clean_required` | 否 | clean 操作必填参数名；无则省略 |
| `clean_optional` | 否 | clean 操作可选参数名 |
| `query_required` | 否 | **已弃用**（解析兼容保留）：query 不再强制必填参数，precheck 对 query 不做必填校验。query 参数应声明在 `query_optional` |
| `query_optional` | 否 | query 操作可选参数名（逗号分隔）；无参时脚本自行展示全部（如全部核/全部网卡），有参则按参过滤 |

> 所有故障统一同步阻塞执行；需要长驻的故障由脚本自行管理子进程。

### 3.2 参数语义约定

- **必填 vs 可选**：`inject_required`/`clean_required` 在 precheck 阶段按操作分别校验非空；**`query` 不强制必填**（无参时脚本展示全部，有参则过滤），query 参数声明在 `query_optional`。`inject_optional`/`clean_optional`/`query_optional` 缺省时不报错，由脚本解释默认值。空字段可省略（默认空字符串）。

### 3.3 目录清单

> 标 `*` 为示例故障（rCPU_overload / rNET_delay）。完整 cnf 声明见 §7。
> 上表仅列 inject 操作的 required/optional 参数；clean / query 的 per-op 参数见 `demoncat.conf`（§7）。

| UID | module | supported_ops | inject_required | inject_optional |
| --- | --- | --- | --- | --- |
| `rCPU_overload` * | cpu | inject,clean,query | cores | load_pct |
| `rNET_delay` * | network | inject,clean,query | iface,delay_ms | — |
| `rNET_loss` | network | inject,clean,query | iface,loss_pct | — |
| `rNET_reorder` | network | inject,clean,query | iface,reorder_pct | — |
| `rNET_down` | network | inject,clean,query | iface | — |
| `rNET_degrade` | network | inject,clean,query | iface | speed_mbps(默认10) |
| `rNET_port_occupy` | network | inject,clean,query | port | protocol(默认tcp) |
| `rNET_service_stop` | network | inject,clean,query | service | — |
| `rNET_link_flap` | network | inject,clean,query | iface | cycle_sec(默认2),count(默认10) |
| `rNET_bw_limit` | network | inject,clean,query | iface,rate_kbps | — |
| `rNET_jitter` | network | inject,clean,query | iface,delay_ms,jitter_ms | — |
| `rNET_tcp_loss` | network | inject,clean,query | port | direction(默认both) |
| `rPROC_exit` | process | **inject** | pid | — |
| `rPROC_hang` | process | inject,clean,query | pid | — |
| `rPROC_zstate` | process | inject,clean,query | pid | — |
| `rCPU_core_offline` | cpu | inject,clean,query | cores | — |
| `rDISK_write_overload` | storage | inject,clean,query | device | workers(默认4),size_mb(默认200) |
| `rNPU_link_down` | npu | inject,clean,query | chip | — |
| `rNPU_ip_change` | npu | inject,clean,query | chip,address,netmask | — |
| `rNPU_gw_change` | npu | inject,clean,query | chip,gateway | — |
| `rNPU_netdetect_change` | npu | inject,clean,query | chip,address | — |
| `rNPU_arp` | npu | inject,clean,query | chip,action,dev,ip | mac |
| `rNPU_route` | npu | inject,clean,query | chip,action,address,netmask | gateway |
| `rNPU_iprule` | npu | inject,clean,query | chip,action,dir,ip | table |
| `rNPU_iproute` | npu | inject,clean,query | chip,action,ip,ip_mask,table | via,dev |
| `rNPU_bw_limit` | npu | inject,clean,query | chip,bw_limit | — |
| `rNPU_mtu_mismatch` | npu | inject,clean,query | chip,size | — |
| `rNPU_dscp_tc_change` | npu | inject,clean,query | chip,dscp,tc | — |
| `rNPU_roce_port_change` | npu | inject,clean,query | chip,port | — |
| `rCPU_quota` | cpu | inject,clean,query | cores,quota_pct | — |
| `rCPU_freq` | cpu | inject,clean,query | cores,freq_mhz | — |
| `rCPU_core_hang` | cpu | inject,clean,query | cores | — |
| `rDISK_part_full` | storage | inject,clean,query | path | size |
| `rDISK_inode_exhaust` | storage | inject,clean,query | path | count |
| `rDISK_io_delay` | storage | inject,clean,query | device,delay_ms | — |
| `rDISK_io_error` | storage | inject,clean,query | device | — |
| `rNET_corrupt` | network | inject,clean,query | iface,corrupt_pct | — |
| `rNET_conn_exhaust` | network | inject,clean,query | target | count |
| `rPROC_fork_bomb` | process | inject,clean,query | count | — |
| `rPROC_fd_exhaust` | process | inject,clean,query | count | — |
| `rMEM_leak` | memory | inject,clean,query | size_mb | — |
| `rMEM_oom` | memory | inject,clean,query | rate_mb | — |
| `rMEM_fragment` | memory | inject,clean,query | blocks | block_kb |
| `rMEM_swap_overload` | memory | inject,clean,query | size_mb | — |
| `rFS_file_lock` | filesystem | inject,clean,query | path,mode | — |
| `rFS_iowait_high` | filesystem | inject,clean,query | path | workers |
| `rDOCKER_kill` | docker | inject,clean,query | container | — |
| `rDOCKER_mem_overload` | docker | inject,clean,query | container,size | — |
| `rNPU_pcie_down` | npu | inject,clean,query | npu_id | gen |
| `rNPU_aic_load` | npu | inject,clean,query | chip | load_pct |
| `rNPU_aiv_load` | npu | inject,clean,query | chip | load_pct |
| `rNPU_hbm_load` | npu | inject,clean,query | chip,size | — |
| `rNPU_chip_reset` | npu | inject,clean,query | npu_id | core |
| `rNPU_driver_unbind` | npu | inject,query | chip | — |
| `rNPU_pcie_remove` | npu | inject,query | chip | — |
| `rSYS_panic` | system | inject | — | — |
| `rSYS_poweroff` | system | inject | mode | — |

### 3.4 扩展约定

- **新增模块**：在 `module` 字段取新值（如 `memory`），脚本放到 `src/scripts/<module>/`，cnf 加段即可。`npu` 模块已落地（见 §3.3 `rNPU_*`）。
- **现有模块加故障**：同模块目录加脚本 + cnf 加段，UID 不重复即可。
- 目录将持续扩充（预计 200+），不预设模块实现先后顺序，按发布批次推进（见 §8）。

新增故障的 cnf 段按**每操作**声明 required/optional 参数（空字段可省略，默认空字符串）：

```ini
[fault.rNET_loss]
module          = network
script          = /usr/lib/demoncat/scripts/network/net_loss.sh
supported_ops   = inject,clean,query
inject_required = iface,loss_pct
inject_optional = direction
clean_required  = iface
clean_optional  = direction
query_required  =              # 已弃用（解析兼容保留，留空）；query 参数放 query_optional
query_optional  = iface,direction
```

---

## 4. 预检

### 4.1 预检概述

所有 `inject` 请求按固定顺序校验，任一失败即中止并返回错误 JSON（退出码 3 或对应码）。`clean` / `query` 请求走 §4.2 第 1、2、4 步子集（见下）。完整 4 步详述见 §4.2。

### 4.2 预检 4 步详述

按以下顺序校验，任一失败即中止并返回错误 JSON（退出码 3 或对应码）：

1. uid 在配置中存在（否则退出码 4）
2. 请求的 op 属于该故障 `supported_ops`（`inject`-only 故障拒绝 `clean`/`query`）
3. 对应操作的 `inject_required` / `clean_required` 全部提供且非空（按操作分别校验；**`query` 不做必填校验**，无参合法）
4. 脚本路径存在且可执行（`access/X_OK`）

> 允许同 uid 重复注入（含相同参数），dcat 不做并发拦截；脚本自行处理幂等性。
> `clean` / `query` 请求校验第 1、2、4 步；`clean` 按用户参数匹配活跃记录（`state_find_by_params`）。**`clean <uid>` 带参数时按参数匹配 state 记录逐条清理；`clean <uid>` 无参数时走 stateless 路径（脚本自行 glob `/tmp` 工件清理该 uid 全部注入，不查 state，`clean_required` 在零参数时跳过校验）；`clean --all` 对全部支持 clean 的故障 fan-out 无参 clean（stateless）。`query`（带 uid）不强制必填参数——无参时脚本自行展示全部（如全部核/全部网卡/读 sidecar），有参则按参过滤；仅校验参数是否已声明（未声明的 `--foo=bar` 仍拒绝，退出码 3）。`query`（不带 uid）查全部活跃记录，不受此限制。**
> 所有命令（inject / clean / query）均校验用户提供的参数是否在该操作对应的 required/optional 列表（`inject_required`/`inject_optional`、`clean_required`/`clean_optional`、`query_required`/`query_optional`）中声明，未声明的参数（`--foo=bar`）直接拒绝（退出码 3）。
>
> **参数校验分层**：dcat 负责**结构校验**（`inject_required`/`clean_required` 是否齐全且非空，即第 3 步；**`query` 不强制必填**）；参数值的**语义校验**（如核号范围、iface 是否存在、chip 是否有效）由脚本自行校验。

---

## 5. 脚本契约

dcat 通过环境变量向脚本传递操作与参数（免 shell 注入、语言无关）：

| 环境变量 | 含义 |
| --- | --- |
| `DCAT_OP` | `inject` / `clean` / `query` |
| `DCAT_UID` | 故障 uid |
| `DCAT_PARAM_<KEY>` | 每个参数，KEY 为参数名大写、非字母数字字符替换为 `_` |

### 5.1 通用约定

- **退出码**：`0` 成功；非 0 失败。
- **stdout**：成功时可选输出一行说明文本，dcat 纳入结果 JSON 的 `data.message`。
- **stderr**：失败时输出错误信息，dcat 纳入 `error.message`。
- 可选参数未提供时，对应 `DCAT_PARAM_<KEY>` 环境变量不设置；脚本须自行处理默认值（目录表中以 `(默认X)` 标注期望默认）。
- **同步阻塞**：dcat 用 `fork/exec + waitpid` 同步等待脚本执行完返回；脚本应自行管理长驻子进程（spawn + pidfile/sidecar），不要前台驻留阻塞 dcat。
- **参数校验边界**：dcat 在 precheck 阶段按操作校验 `inject_required`/`clean_required` 齐全且非空（结构校验）；**`query` 不强制必填**（无参时脚本展示全部）。参数值合法性（如 `cores` 是否在有效范围、`iface` 是否真实存在）由脚本自行校验（语义校验）。未在该操作对应 required/optional 列表中声明的参数直接拒绝（报错退出码 3）。

### 5.2 可恢复故障（`inject,clean,query`）

- **inject**：脚本执行完即返回。需要长驻的故障（如 CPU 过载、端口占用、僵尸生成、磁盘写压），脚本自行 spawn 子进程并写 pidfile/sidecar 到约定位置（如 `/tmp/dcat-<uid>.pid`）后立即返回。
- **clean**：dcat 按用户提供的参数匹配活跃记录。dcat 传记录存储的 inject 参数给脚本 `DCAT_OP=clean`，脚本据此清理资源（删 qdisc / 删 iptables 规则 / 起服务 / 读 pidfile kill 子进程）。多条记录匹配时，逐条执行 clean 脚本；某条失败时停止，剩余记录不清理。脚本退出码非 0 时 dcat 报错且**不 mark inactive**（故障可能仍在系统上）。**无参 clean**：`clean <uid>` 无参数 或 `clean --all` 走 stateless 路径——脚本自行 glob `/tmp/dcat-<uid>-*` 工件（pidfile/sidecar/.bak）清理，不查 state.json；state.json 丢失/损坏时仍可清。脚本 clean 输出约定**仅一行**汇总（executor 单次 read pipe 后即关，多行写入会触发 SIGPIPE 误报失败）。
- **`query`（无 uid）**：由 dcat 自身 state 回答（遍历活跃记录），**不调用脚本**。
- **`query`（有 uid）**：调脚本 `DCAT_OP=query` 分支验证故障是否实际生效，详见 §5.4。

### 5.3 一次性故障（`inject`-only）

- 无 `clean` op、无 `query` op、无 state 记录。
- inject 脚本执行完即终结。
- 典型：`rPROC_exit`（`kill -9`，进程已死不可逆）。
- dcat 在 `inject` 成功后直接 `output_ok`，不写 state。
- `dcat query rPROC_exit` 在 precheck 阶段拒绝（query 不在 supported_ops，退出码 3）。

### 5.4 query 分支（有 uid，故障验证）

**有 uid 的 query** 调用脚本 `DCAT_OP=query` 分支验证故障是否真的在系统上生效。可恢复故障脚本（`supported_ops` 含 `query`）须实现该分支：

- dcat 通过环境变量传入**用户当前输入的参数**（`DCAT_PARAM_*`），**不是** inject 时的参数——用户可注入 CPU1 满载后查询 CPU2 的负载。
- **query 不强制必填参数**：`dcat query <uid>`（无参）合法，脚本应在此情况下展示**全部**相关资源（如 `rCPU_overload` 查全部在线核、`rNET_*` 查全部网卡或读 sidecar）；`dcat query <uid> --key=value` 则按参过滤。precheck 对 query 不做必填校验，仅校验参数是否已声明。
- 脚本检查**实际系统状态**（如 `top`/`tc qdisc show`/`pgrep`/`sysfs`），输出任意格式的证据文本（表格、多行文本）到 stdout。
- **退出码**：`0` = 故障确认生效 / 非 `0` = 未生效。
- **输出格式**（方案 A）：dcat 原样输出脚本 stdout，然后打印 `---` 分隔符，最后输出 JSON：
  ```text
  <脚本原始输出（表格/文本）>
  ---
  {"status":"ok","op":"query","uid":"rCPU_overload","data":{"confirmed":true}}
  ```

### 5.5 reinject 默认拒绝 + --force 原子替换

对同一资源的重复注入，dcat **默认拒绝**（退出码 5），需显式 `--force` 才原子替换。

- **资源键** = 故障的 `clean_required` 各参数（资源标识，非值参数）。例：`rCPU_overload` 资源键=`cores`；`rNET_delay` 资源键=`iface`；`rNPU_arp` 资源键=`chip,dev,ip`。
- **重叠判定**：
    - `cores` 参数走**集合交集**（核集语义）：`0,1` 与 `0-8` 重叠（核 0、1 相交），`0,1` 与 `2,3` 不重叠。
    - 其余参数（device/iface/port/pid/chip/...）走**精确等值**；多参资源键（如 chip,dev,ip）各参精确 AND。
- **默认拒绝**：inject 时若已有同 uid + 同资源键重叠的活动记录 → 返回 `code:5` 拒绝，message 列出重叠记录的 record id + 其参数（最多 3 条，超出显示 `+N more`），如 `resource already injected (record id 3: cores=0,1); use --force to replace`，不执行注入。
- **--force 原子替换**：`dcat inject <uid> ... --force` → 先逐个 clean 所有重叠记录（复用 clean 路径），全成功后再 inject。非真原子（两步脚本，有窗口）：clean 中途失败则中止后续注入（**已 clean 的记录保持已清理**，message 带出失败的 record id + 底层脚本错误；操作者可再次 `--force` 重试清理剩余并注入）；注入失败则旧已清（操作者可重注）。
- **作用范围**：v1 仅对 catalog（CNF/config 驱动）故障生效；插件与 legacy injector 路径暂不接入 reinject 检测（同资源重注入不拒绝、`--force` 不替换）。
- **不同资源并发**：不重叠的资源（不同核段/不同 iface/不同 pid）仍可并发注入，各自独立 state 记录。
- **inject-only 故障**：不写 state → 无活动记录 → 天然不触发拒绝（如 `rPROC_exit` 可重复 inject 同 pid）。
- **`--force` 仅 inject 路径生效**；clean/query/list 上出现 `--force` 被忽略（不报错，兼容历史脚本）。`--force=x`（带值）报解析错误。

**BREAKING 变更**（相对 v0.1 加法并集）：`rCPU_overload` 同核/重叠核重注入从"幂等共存"改为"默认拒绝"。迁移：重注入改加 `--force`；不同核并发不变。

示例：

```bash
# 同资源重注入 → 默认拒绝 (code 5)
dcat inject rCPU_overload --cores=0,1
dcat inject rCPU_overload --cores=0,1           # REJECT
dcat inject rCPU_overload --cores=0,1 --force   # 原子替换

# 重叠核集 → 拒绝
dcat inject rCPU_overload --cores=0,1
dcat inject rCPU_overload --cores=0-8           # REJECT (核集相交)
dcat inject rCPU_overload --cores=0-8 --force   # 替换

# 不同资源 → 并发 OK
dcat inject rCPU_overload --cores=0,1
dcat inject rCPU_overload --cores=2,3           # OK (不重叠)
dcat inject rNET_delay --iface=eth0 --delay_ms=100
dcat inject rNET_delay --iface=eth1 --delay_ms=100   # OK (不同 iface)
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
{"status":"error","op":"inject","uid":"rCPU_overload","error":{"code":3,"message":"missing required param: cores"}}
```

**`query`（无 uid，state 查询）**：

```json
{"status":"ok","op":"query","data":[{"uid":"rCPU_overload","record_id":3,"started_at":1721000000,"active":true,"params":{"cores":"4"}}]}
```

**`query`（有 uid，故障验证 — 方案 A 输出）**：

```text
yes_processes: 2
--- cpu usage ---
%Cpu(s): 98.0 us, 1.0 sy, 0.0 ni, 0.0 id
---
{"status":"ok","op":"query","uid":"rCPU_overload","data":{"confirmed":true}}
```

> 脚本原始输出在前，`---` 分隔，JSON 在后。`confirmed: true` = 故障确认生效。

**`list`**：

```json
{"status":"ok","op":"list","data":[{"uid":"rCPU_overload","module":"cpu","supported_ops":["inject","clean","query"],"desc":"..."}]}
```

### 6.1 退出码

| 退出码 | 含义 |
| --- | --- |
| 0 | 成功 |
| 1 | 运行错误（脚本非 0 退出、fork/exec 失败等） |
| 2 | 解析错误（命令格式不合法） |
| 3 | 预检拒绝 |
| 4 | 未找到（uid 不在目录中） |
| 5 | 重注入拒绝（同资源已注入，需 `--force`） |

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
inject_required = iface,loss_pct
inject_optional = direction
clean_required  = iface
clean_optional  = direction
query_required  =              # 已弃用，留空
query_optional  = iface,direction

[fault.rPROC_exit]
module          = process
desc            = 进程异常退出（kill -9，不可恢复）
script          = /usr/lib/demoncat/scripts/process/proc_exit.sh
supported_ops   = inject
inject_required = pid
```

每个 `[fault.<uid>]` 段按**每操作**声明 required/optional 参数，共 6 个字段：`inject_required` / `inject_optional` / `clean_required` / `clean_optional` / `query_required` / `query_optional`。空字段可省略（默认空字符串）。仅 `inject` 的故障（`supported_ops = inject`）只需声明 `inject_*`。

### 7.1 配置定位

`dcat` 通过 `/proc/self/exe` 解析自身路径，推导出**固定相对路径**：

```text
<binary_dir>/../config/demoncat.conf
```

例如 `dcat` 位于 `/opt/dcat/build/dcat` 时，配置固定在 `/opt/dcat/config/demoncat.conf`。配置、脚本目录与二进制一同部署，无需环境变量、无需拷贝。`--config <path>` 可覆盖此默认值（测试/特殊场景）。

---

## 8. 发布批次

DemonCAT 故障按需求增量推进，**不按模块预设先后顺序**。新增模块（如 `memory`）或在现有模块内加故障均属正常扩充。

| 批次 | 范围 | 状态 |
| --- | --- | --- |
| **v0.1** | 核心框架 + 33 条故障（cpu 2 / network 11 / process 3 / storage 1 / npu 16）+ 测试 | ✅ 已完成 |
| **v0.1.0** | 扩展至 58 条故障（9 模块：cpu 5 / storage 5 / network 13 / process 5 / memory 4 / filesystem 2 / docker 2 / npu 20 / system 2）+ 27 ctest / 58 scripts / 479 e2e steps / 249 flows | ✅ 已完成 |

每批次的实现内容 = `src/scripts/` 加脚本 + `demoncat.conf` 加段 + `tests/ut/test_faults_*.c` 加表驱动用例；**不修改二进制核心**（开闭原则）。

---

## 9. 测试策略

### 9.1 测试流程要求

1. **TDD 流程**：每个功能模块按以下顺序开发——①编写测试用例（CTest + mock_executor，定义 inject/clean/query 的期望命令串、环境变量、退出码、JSON 输出）→ ②实现功能代码 → ③运行测试直到全绿。不允许先写实现再补测试。
2. **每增加一个故障，必须验证 inject / clean / query 三路径**（`inject`-only 故障仅验证 inject）。测试不通过则修改脚本/配置重新测试，直到通过。
3. **每完成一个发布批次，做一次完整测试**，ctest 全绿。
4. 测试过程中遇到的问题自行解决，不依赖外部协助。

### 9.2 测试覆盖范围

| 层级 | 范围 | 工具 | 测试文件 |
| --- | --- | --- | --- |
| 单元测试 | cli 解析、registry 查找、预检全路径、state 记录 | CTest + mock_executor | test_cli / test_registry / test_precheck / test_state |
| 执行器 mock | executor_run/run_raw 的 mock 钩子 | CTest | test_executor_mock |
| 输出格式 | result_t 构建/打印/释放 | CTest | test_output |
| 表驱动故障 | 58 故障的 inject/clean/query 下发命令串 + env | CTest + mock_executor | test_faults（通用）+ test_faults_network / test_faults_process / test_faults_cpu_storage / test_faults_npu（按模块） |
| 真实脚本测试 | 2 个示例故障用 mock（不断言真 CPU / 真 tc） | CTest | 同上 |
| 端到端冒烟 | 真实 dcat 二进制：inject→query→clean→query→无残留 | 手工冒烟 | — |

### 9.3 mock_executor

`executor_set_mock(fn)`：`fn` 捕获 `(cmd, env)` 不真正 fork，返回伪造 `result_t`。测试可断言下发命令串与环境变量集合，避免真硬件依赖。

---

## 10. 非功能性需求

| 项目 | 要求 |
| --- | --- |
| 日志 | stderr 输出；级别由 `log_level` 控制（`debug/info/warn/error`）；生产默认 `warn` |
| 错误隔离 | 单个故障 inject/clean 失败不影响 dcat 主流程与其他故障 |
| 资源占用 | 静态二进制；核心路径零动态分配 |
| 跨平台 | Linux（glibc/musl）；WSL 兼容；不要求 Windows |
| 可测 | mock_executor + 表驱动；无硬件可测全部 58 故障的下发命令串 |
| 状态持久化 | state 变更后写 `~/.demoncat/state.json`（cJSON 序列化），启动加载恢复 record_id 计数与未清理记录 |

---

## 11. 高级扩展点

少数需要进程内自定义逻辑（精确定时、二进制协议）的故障可走**编译注入器**路径：实现 `injector_t`（`uid` + 4 个函数指针 `inject/clean/query/precheck`）并注册到 `builtin_injectors[]`。registry 查找时 cnf 故障优先，未命中再查编译注入器。

> 本期 YAGNI，仅文档与头文件 `src/injectors/injector.h` 留位。所有故障均走 cnf + 脚本路径。

### 11.1 动态插件层（第三层 dispatch）

除 cnf + 脚本（第一层）与编译注入器（第二层）外，dispatch 还支持**第三层动态插件**（`dlopen` 加载的 `.so`），运行时可插拔，无需重编译 dcat。三层按优先级回退：

1. **cnf 数据驱动**（`registry_find` 命中 `fault_def_t`）→ executor 调脚本
2. **编译注入器**（`injector_find` 命中 `injector_t`）→ 直接函数调用
3. **动态插件**（`plugin_find` 命中 `dcat_plugin_t`）→ `plugin_dispatch` 调函数指针

插件接口 `dcat_plugin_t`（`src/plugins/plugin.h`）含 ABI 版本门控 + per-op 参数声明 + `init`/`fini` 生命周期钩子 + `precheck`/`inject`/`clean`/`query` 函数指针。插件目录默认 `<root>/plugins`，`--plugins <dir>` 可覆盖。完整设计见 [docs/Dynamic_Plugin_Implement.md](docs/Dynamic_Plugin_Implement.md)。

> 示例插件 `src/plugins/sample/sample_plugin.c`（`rSAMPLE_test`），本期仅用于集成测试。

---

## 12. Web 控制平面（dcat serve）

`dcat serve` 在二进制内内置 HTTP 控制平面 + 静态前端（`src/web/`），把故障目录、活跃注入、历史记录搬到浏览器。默认**只读**（`--allow-write` 开启注入/清理），无外部 HTTP 依赖。

- **子命令**：`dcat serve [--port N] [--bind ADDR] [--webroot DIR] [--allow-write]`
- **默认**：端口 8080，绑定 `0.0.0.0`（SSH 隧道访问可用 `--bind 127.0.0.1`）
- **API 端点**：`/api/state`（活跃记录）、`/api/history`（历史）、`/api/catalog`（目录）、`/api/inject`（需 `--allow-write`）、`/api/clean`（需 `--allow-write`）
- **安全**：`realpath()` 路径穿越防护 + `%2e` URL 编码检测 + `--port` CLI 校验
- **state 同步**：`/api/state` 与 `/api/history` 从磁盘 reload `state.json`，同步命令行修改

> 详见 [User_Manual.md 第六章](User_Manual.md)。
