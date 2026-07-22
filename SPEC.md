# DemonCAT 技术规格说明书 (SPEC)

> **DemonCAT**（简称 **dcat**）— Demon Computing Availability Tools
> Linux 计算故障注入工具：统一的命令面、预检护栏、状态跟踪；具体故障以**外部脚本 + 声明式配置**接入。

---

## 1. 概述

### 1.1 软件定位

DemonCAT 是面向计算系统（CPU / 内存 / 存储 / 网络 / 进程 / NPU）的**故障注入工具**。其核心价值不是实现每一种故障原语，而是提供**统一命令面、状态跟踪**，把具体故障以**外部脚本 + 声明式配置**的方式接入。加一个故障 = 加一个脚本 + 配置文件一行，**免重新编译**。

### 1.2 技术栈

| 项目 | 选型 |
|------|------|
| 开发语言 | C11（ISO/IEC 9899:2011） |
| 构建 | CMake ≥ 3.10 |
| 目标平台 | Linux（glibc / musl），WSL 兼容 |
| 第三方依赖 | 仅 cJSON 单文件库（vendored 进仓库 `third_party/cjson/`） |
| 线程 | pthread（状态锁） |
| 配置文件 | INI（`demoncat.conf`） |
| 数据输出 | stdout JSON |

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

```
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

- `subcommand` ∈ `{ inject, clean, query, list }`
- `uid`：故障唯一标识（如 `rCPU_overload`），在配置中声明；`query` 和 `list` 可省略 uid
- 所有参数以 `--key=value` 标志传入，可选参数不填即省略对应标志
- 全局选项 `--config` / `--help` 与参数标志混合使用

### 2.1 命令集

| 命令 | 语义 | 适用 supported_ops |
|---|---|---|
| `inject <uid> --p1=v1 --p2=v2 ...` | 注入指定故障，按参数配置；**同步阻塞执行脚本，执行完返回** | `inject` 或 `inject,clean,query` |
| `clean <uid> [--k1=v1 ...]` | 清除该 uid 活跃注入；按参数匹配记录，逐条清理；同步重跑脚本 `DCAT_OP=clean` | 仅 `inject,clean,query` |
| `query [uid] [--k1=v1 ...]` | 无 uid：查询 dcat 自身全部活跃注入记录；有 uid：调脚本 `query` 分支验证故障是否真的在系统上生效，用户参数与 inject 参数独立 | `inject,clean,query` |
| `list` | 列出配置中声明的全部故障目录 | 所有 |

> `inject`-only 故障（如 `rPROC_exit`）不支持 `clean` / `query`；注入即终结，无活跃记录。
> 本期不实现超时自动恢复；所有可恢复故障注入后需用户手动 `clean`。

### 2.2 示例

```
# 可恢复故障：需手动 clean
dcat inject rNET_loss --iface=eth0 --loss_pct=5
dcat clean rNET_loss --iface=eth0

# 并发注入同 uid 不同参数
dcat inject rNET_loss --iface=eth0 --loss_pct=5
dcat inject rNET_loss --iface=eth1 --loss_pct=3
dcat clean rNET_loss --iface=eth0    # 只清 eth0

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
|---|---|---|
| `module` | 是 | 模块归类：`cpu` / `memory` / `storage` / `network` / `process` / `npu` |
| `desc` | 否 | 一句话描述 |
| `script` | 是 | 外部脚本路径（绝对或相对；相对路径基于项目根目录自动解析为绝对），须可执行 |
| `supported_ops` | 是 | 支持的操作子集，逗号分隔：`inject` 或 `inject,clean,query` |
| `required_params` | 否 | **必填**参数名（逗号分隔），预检校验完整性，缺失即拒；无必填参数时省略此字段 |
| `optional_params` | 否 | **可选**参数名（逗号分隔），不填时脚本走自有默认值 |

> 所有故障统一同步阻塞执行；需要长驻的故障由脚本自行管理子进程。

### 3.2 参数语义约定

- **必填 vs 可选**：`required_params` 在 precheck 阶段校验非空；`optional_params` 缺省时不报错，由脚本解释默认值（目录表中以 `(默认X)` 标注）。

### 3.3 目录清单（共 38 条）

> 标 `*` 为示例故障（rCPU_overload / rNET_delay）。完整 cnf 声明见 §7。

| UID | module | supported_ops | required_params | optional_params |
|---|---|---|---|---|
| `rCPU_overload` * | cpu | inject,clean,query | cores | — |
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
| `rPROC_dstate` | process | inject,clean,query | count | — |
| `rPROC_hang` | process | inject,clean,query | pid | — |
| `rPROC_zstate` | process | inject,clean,query | count | — |
| `rCPU_core_offline` | cpu | inject,clean,query | cores | — |
| `rDISK_write_overload` | storage | inject,clean,query | device | workers(默认4) |
| `rNPU_link_down` | npu | inject,clean,query | chip | — |
| `rNPU_ip_change` | npu | inject,clean,query | chip,address,netmask | — |
| `rNPU_gw_change` | npu | inject,clean,query | chip,gateway | — |
| `rNPU_netdetect_change` | npu | inject,clean,query | chip,address | — |
| `rNPU_arp_poison` | npu | inject,clean,query | chip,dev,ip,mac | — |
| `rNPU_arp_del` | npu | inject,clean,query | chip,dev,ip | — |
| `rNPU_route_add` | npu | inject,clean,query | chip,address,netmask,gateway | — |
| `rNPU_route_del` | npu | inject,clean,query | chip,address,netmask | — |
| `rNPU_route_clear` | npu | inject,clean,query | chip | — |
| `rNPU_iprule_add` | npu | inject,clean,query | chip,dir,ip,table | — |
| `rNPU_iprule_del` | npu | inject,clean,query | chip,dir,ip | — |
| `rNPU_iproute_add` | npu | inject,clean,query | chip,ip,ip_mask,via,dev,table | — |
| `rNPU_iproute_del` | npu | inject,clean,query | chip,ip,ip_mask,table | — |
| `rNPU_bw_limit` | npu | inject,clean,query | chip,bw_limit | — |
| `rNPU_mtu_mismatch` | npu | inject,clean,query | chip,size | — |
| `rNPU_fec_change` | npu | inject,clean,query | chip,encoding | — |
| `rNPU_dscp_tc_change` | npu | inject,clean,query | chip,dscp,tc | — |
| `rNPU_prio_tc_change` | npu | inject,clean,query | chip,map | — |
| `rNPU_pfc_change` | npu | inject,clean,query | chip,bitmap | — |
| `rNPU_roce_port_change` | npu | inject,clean,query | chip,port | — |

### 3.4 扩展约定

- **新增模块**：在 `module` 字段取新值（如 `memory`），脚本放到 `config/scripts/<module>/`，cnf 加段即可。`npu` 模块已落地（见 §3.3 `rNPU_*`）。
- **现有模块加故障**：同模块目录加脚本 + cnf 加段，UID 不重复即可。
- 目录将持续扩充（预计 200+），不预设模块实现先后顺序，按发布批次推进（见 §8）。

---

## 4. 预检

### 4.1 预检概述

所有 `inject` 请求按固定顺序校验，任一失败即中止并返回错误 JSON（退出码 3 或对应码）。`clean` / `query` 请求走 §4.2 第 1、2、4 步子集（见下）。完整 4 步详述见 §4.2。

### 4.2 预检 4 步详述

按以下顺序校验，任一失败即中止并返回错误 JSON（退出码 3 或对应码）：

1. uid 在配置中存在（否则退出码 4）
2. 请求的 op 属于该故障 `supported_ops`（`inject`-only 故障拒绝 `clean`/`query`）
3. `inject`：`required_params` 全部提供且非空
4. 脚本路径存在且可执行（`access/X_OK`）

> 允许同 uid 重复注入（含相同参数），dcat 不做并发拦截；脚本自行处理幂等性。
> `clean` / `query` 请求校验第 1、2、4 步；`clean` 按用户参数匹配活跃记录（`state_find_by_params`）。
> 所有命令（inject / clean / query）均校验用户提供的参数是否在 `required_params` / `optional_params` 中声明，未声明的参数（`--foo=bar`）直接拒绝（退出码 3）。

> **参数校验分层**：dcat 负责**结构校验**（`required_params` 是否齐全且非空，即第 3 步）；参数值的**语义校验**（如核号范围、iface 是否存在、chip 是否有效）由脚本自行校验。

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
- **同步阻塞**：dcat 用 `fork/exec + waitpid` 同步等待脚本执行完返回；脚本应自行管理长驻子进程（spawn + pidfile/sidecar），不要前台驻留阻塞 dcat。
- **参数校验边界**：dcat 在 precheck 阶段校验 `required_params` 齐全且非空（结构校验）；参数值合法性（如 `cores` 是否在有效范围、`iface` 是否真实存在）由脚本自行校验（语义校验）。未在 `required_params` / `optional_params` 中声明的参数直接拒绝（报错退出码 3）。

### 5.2 可恢复故障（`inject,clean,query`）

- **inject**：脚本执行完即返回。需要长驻的故障（如 CPU 过载、端口占用、僵尸生成、磁盘写压），脚本自行 spawn 子进程并写 pidfile/sidecar 到约定位置（如 `/tmp/dcat-<uid>.pid`）后立即返回。
- **clean**：dcat 按用户提供的参数匹配活跃记录。dcat 传记录存储的 inject 参数给脚本 `DCAT_OP=clean`，脚本据此清理资源（删 qdisc / 删 iptables 规则 / 起服务 / 读 pidfile kill 子进程）。多条记录匹配时，逐条执行 clean 脚本；某条失败时停止，剩余记录不清理。脚本退出码非 0 时 dcat 报错且**不 mark inactive**（故障可能仍在系统上）。
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
{"status":"error","op":"inject","uid":"rCPU_overload","error":{"code":3,"message":"missing required param: cores"}}
```

**`query`（无 uid，state 查询）**：
```json
{"status":"ok","op":"query","data":[{"uid":"rCPU_overload","record_id":3,"started_at":1721000000,"active":true,"params":{"cores":"4"}}]}
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
{"status":"ok","op":"list","data":[{"uid":"rCPU_overload","module":"cpu","supported_ops":["inject","clean","query"],"desc":"..."}]}
```

### 6.1 退出码

| 退出码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 运行错误（脚本非 0 退出、fork/exec 失败等） |
| 2 | 解析错误（命令格式不合法） |
| 3 | 预检拒绝 |
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

[fault.rPROC_exit]
module          = process
desc            = 进程异常退出（kill -9，不可恢复）
script          = /usr/lib/demoncat/scripts/process/proc_exit.sh
supported_ops   = inject
required_params = pid
```

### 7.1 配置定位

`dcat` 通过 `/proc/self/exe` 解析自身路径，推导出**固定相对路径**：

```
<binary_dir>/../config/demoncat.conf
```

例如 `dcat` 位于 `/opt/dcat/build/dcat` 时，配置固定在 `/opt/dcat/config/demoncat.conf`。配置、脚本目录与二进制一同部署，无需环境变量、无需拷贝。`--config <path>` 可覆盖此默认值（测试/特殊场景）。

---

## 8. 发布批次

DemonCAT 故障总量预计 200+，按需求增量推进，**不按模块预设先后顺序**。新增模块（如 `memory`）或在现有模块内加故障均属正常扩充。

| 批次 | 范围 | 状态 |
|---|---|---|
| **v0.1** | 核心框架 + 38 条故障（cpu 2 / network 11 / process 4 / storage 1 / npu 20）+ 测试 | 待开发 |

每批次的实现内容 = `config/scripts/` 加脚本 + `demoncat.conf` 加段 + `tests/test_faults.c` 加表驱动用例；**不修改二进制核心**（开闭原则）。

---

## 9. 测试策略

### 9.1 测试流程要求

1. **TDD 流程**：每个功能模块按以下顺序开发——①编写测试用例（CTest + mock_executor，定义 inject/clean/query 的期望命令串、环境变量、退出码、JSON 输出）→ ②实现功能代码 → ③运行测试直到全绿。不允许先写实现再补测试。
2. **每增加一个故障，必须验证 inject / clean / query 三路径**（`inject`-only 故障仅验证 inject）。测试不通过则修改脚本/配置重新测试，直到通过。
3. **每完成一个发布批次，做一次完整测试**，ctest 全绿。
4. 测试过程中遇到的问题自行解决，不依赖外部协助。

### 9.2 测试覆盖范围

| 层级 | 范围 | 工具 | 测试文件 |
|---|---|---|---|
| 单元测试 | cli 解析、registry 查找、预检全路径、state 记录 | CTest + mock_executor | test_cli / test_registry / test_precheck / test_state |
| 执行器 mock | executor_run/run_raw 的 mock 钩子 | CTest | test_executor_mock |
| 输出格式 | result_t 构建/打印/释放 | CTest | test_output |
| 表驱动故障 | 38 故障的 inject/clean/query 下发命令串 + env | CTest + mock_executor | test_faults（通用）+ test_faults_network / test_faults_process / test_faults_cpu_storage / test_faults_npu（按模块） |
| 真实脚本测试 | 2 个示例故障用 mock（不断言真 CPU / 真 tc） | CTest | 同上 |
| 端到端冒烟 | 真实 dcat 二进制：inject→query→clean→query→无残留 | 手工冒烟 | — |

### 9.3 mock_executor

`executor_set_mock(fn)`：`fn` 捕获 `(cmd, env)` 不真正 fork，返回伪造 `result_t`。测试可断言下发命令串与环境变量集合，避免真硬件依赖。

---

## 10. 非功能性需求

| 项目 | 要求 |
|---|---|
| 日志 | stderr 输出；级别由 `log_level` 控制（`debug/info/warn/error`）；生产默认 `warn` |
| 错误隔离 | 单个故障 inject/clean 失败不影响 dcat 主流程与其他故障 |
| 资源占用 | 静态二进制；核心路径零动态分配 |
| 跨平台 | Linux（glibc/musl）；WSL 兼容；不要求 Windows |
| 可测 | mock_executor + 表驱动；无硬件可测全部 38 故障的下发命令串 |
| 状态持久化 | state 变更后写 `~/.demoncat/state.json`（cJSON 序列化），启动加载恢复 record_id 计数与未清理记录 |

---

## 11. 高级扩展点（本期不实现，仅留位）

少数需要进程内自定义逻辑（精确定时、二进制协议）的故障可走**编译注入器**路径：实现 `injector_t`（`uid` + 4 个函数指针 `inject/clean/query/precheck`）并注册到 `builtin_injectors[]`。registry 查找时 cnf 故障优先，未命中再查编译注入器。

> 本期 YAGNI，仅文档与头文件 `src/injectors/injector.h` 留位。所有故障均走 cnf + 脚本路径。
