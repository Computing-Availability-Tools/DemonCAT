# DemonCAT (dcat)

Linux **计算故障注入 CLI**，覆盖 CPU / 存储 / 网络 / 进程。DemonCAT 是一个**编排器**：
提供统一命令面、安全护栏、状态跟踪与超时自动恢复。具体故障以**外部脚本 + 声明式
INI 配置**接入——加故障 = 加脚本 + 配置文件一行，**免重新编译**。

> 开源 **P0** 版本。完整规格与架构见 [SPEC.md](SPEC.md) 和 [DESIGN.md](DESIGN.md)。

## 功能

- 统一命令格式：`dcat "<command> <uid> [params]"`
- 命令：`inject` / `clean` / `query` / `list`
- query 验证：`dcat "query <uid>"` 调脚本 query 分支检查故障**是否真的在系统上生效**
  （不只是 dcat 内部状态）——输出原始证据（表格/文本）+ JSON `confirmed` 标志
- 数据驱动故障目录（`demoncat.conf`，INI 格式）
- 安全等级：`normal` / `warning`（y/N）/ `dangerous`（需输 `yes`）
- 预检：op 受支持、参数完整、脚本可执行、无重复注入
- 超时故障自动清理（每次调用时 lazy clean + 后台线程 + duration reaper）
- stdout JSON 输出，退出码机器友好
- 核心路径零动态分配，静态二进制
- 参数走环境变量不走 argv，免 shell 注入

## 构建

需要 C11、CMake ≥ 3.10、pthread、gcc。（Linux / WSL 已测试。）

```sh
cmake -B build && cmake --build build
```

`dcat` 二进制在 `build/dcat`。cJSON vendored 在 `third_party/cjson`。

## 快速上手

`dcat` 通过 `/proc/self/exe` 解析自身路径，推导出固定配置路径
`<binary_dir>/../config/demoncat.conf`，**无需 `--config`**，从任意目录运行即可。

```sh
# 列出故障目录
./build/dcat list

# 注入 CPU 满载（2 核，5 秒自动恢复），跳过 warning 确认
./build/dcat "inject rCPU_overload (cores,duration) values (2,5)" --yes

# 查看活跃注入记录（state 查询）
./build/dcat query

# 验证故障是否真的生效（调脚本 query 分支）
# — 原始输出（top 表格）+ --- + JSON 结果
./build/dcat "query rCPU_overload where cores=2"

# 手动清理
./build/dcat "clean rCPU_overload"
```

> **注意：** `--yes` 跳过 `warning` 级确认，但**不跳过** `dangerous`——
> dangerous 故障始终需要交互式输入 `yes`。

### 命令格式

```
dcat "<command> <uid> [(p1,p2) values (v1,v2) | where k1=v1 k2=v2 ...]"
```

| 命令 | 参数形式 | 含义 |
|---|---|---|
| `inject <uid> (p1,p2) values (v1,v2)` | values | 注入故障 |
| `clean <uid> [where k=v ...]` | where | 清除故障（手动或自动） |
| `query [uid] [where k=v ...]` | where | 无 uid：列出活跃 state 记录；有 uid：验证故障是否真的生效（调脚本 query 分支，输出 raw + `---` + JSON） |
| `list` | — | 列出 `demoncat.conf` 声明的故障目录 |

## 脚本依赖的工具

dcat 本身只依赖 C 运行时和 pthread。故障脚本依赖以下 Linux 工具，按模块分组：

### 通用（所有模块）

| 工具 | 来源包 | 用途 |
|---|---|---|
| `cat` / `ls` / `dd` / `yes` / `top` | coreutils | 基础命令 |
| `ps` / `pgrep` | procps-ng | 进程查询 |
| `kill` | util-linux | 信号发送 |
| `mkdir` / `rm` | coreutils | 临时文件管理 |

### CPU 模块

| 工具 | 来源包 | 适用故障 | 说明 |
|---|---|---|---|
| `yes` | coreutils | rCPU_overload | 多核 burn（多实例） |
| `top` / `mpstat` | coreutils / sysstat | rCPU_overload query | CPU 使用率检查（mpstat 可选） |
| sysfs `/sys/devices/system/cpu/` | 内核 | rCPU_core_offline | CPU 核在线/离线 |

### 网络模块

| 工具 | 来源包 | 适用故障 | 说明 |
|---|---|---|---|
| `tc` | iproute2 | delay/loss/reorder/jitter/bw_limit | netem/tbf qdisc |
| `ip` | iproute2 | down/link_flap | 链路状态 |
| `ethtool` | ethtool | degrade | 网卡速率 |
| `iptables` | iptables | tcp_loss | L4 丢包规则 |
| `ss` / `netstat` | iproute2 / net-tools | port_occupy | 端口占用检查 |
| `systemctl` | systemd | service_stop | 服务启停 |
| `python3` | python3 | port_occupy | socket 持有（有 socat/nc 可替代） |

### 进程模块

| 工具 | 来源包 | 适用故障 | 说明 |
|---|---|---|---|
| `kill` | util-linux | exit/hang | kill -9 / kill -STOP / kill -CONT |
| `ps` | procps-ng | dstate/zstate/hang query | 进程状态查询 |
| `/proc/<pid>/status` | 内核 | hang query | 进程状态（T=stopped） |

### 存储模块

| 工具 | 来源包 | 适用故障 | 说明 |
|---|---|---|---|
| `dd` | coreutils | write_overload | 磁盘写压 |
| `pgrep` | procps-ng | write_overload query | dd 进程检查 |

### 一键安装依赖（Debian/Ubuntu）

```sh
apt install coreutils procps-ng util-linux iproute2 ethtool iptables python3
# 可选：
apt install sysstat        # mpstat（CPU query 增强）
apt install net-tools      # netstat（ss 不可用时的 fallback）
```

> WSL 默认已安装 coreutils/procps-ng/util-linux/iproute2。`ethtool`/`iptables`
> 按需安装。网络故障需要 root 权限（CAP_NET_ADMIN）。

## 新增故障

1. 编写可执行脚本，实现 `inject` / `clean` / `query` 分支（见下方*脚本契约*）。
2. 在 `demoncat.conf` 加一段 `[fault.<uid>]`。

```ini
[fault.my_fault]
module         = cpu
desc           = 自定义故障
script         = config/scripts/cpu/my_fault.sh
supported_ops  = inject,clean,query
required_params = a,b
optional_params = duration
safety         = warning
exec_mode      = sync          ; sync | background
timeout        = 0             ; 0 = 仅手动 clean; >0 = N 秒后自动 clean
```

完成——无需重编译，`dcat list` 即可看到新故障。

## 故障目录（v0.2）

4 个模块共 18 条故障（网络 11、进程 4、CPU 2、存储 1）。完整表格见 [SPEC.md §3.3](SPEC.md)。

- **可恢复**（`inject,clean,query`）：dcat 追踪 state；`duration` 可选 → 到期自动恢复。
- **一次性**（`inject`，如 `rPROC_exit`）：一击即终，无 state、无 clean。`clean`/`query` 被拒绝（退出码 3）。

## 脚本契约

dcat 通过**环境变量**向脚本传递操作与参数（不走 argv，免 shell 注入）：

| 变量 | 含义 |
|---|---|
| `DCAT_OP` | `inject` / `clean` / `query` |
| `DCAT_UID` | 故障 uid |
| `DCAT_PARAM_<KEY>` | 每个参数（KEY = 参数名大写、非字母数字替换为 `_`） |

- 退出码 `0` = 成功；非 `0` = 失败。
- `inject` stdout（一行）→ 纳入 `data.message`。
- `query` 分支：检查**实际系统状态**（如 `top`/`tc qdisc show`/`pgrep`），输出证据文本（表格/多行）到 stdout，exit `0` = 故障确认生效 / `1` = 未生效。dcat 输出：脚本原始输出 + `---` + JSON `{"confirmed":true/false}`。
- `background` 模式：脚本须**前台驻留**执行故障（dcat 托管其 pid；`clean` = kill 进程组）。`trap SIGTERM` 清理子进程后退出。
- `sync` 模式：`inject` 执行完返回（长任务可自行 spawn + sidecar pidfile，clean 时读取清理）。`clean` 重跑脚本 `DCAT_OP=clean`（回放 `where` 参数）。脚本退出码非 0 时 dcat 报错且**不 mark inactive**（故障可能仍活跃）。

参考脚本：`config/scripts/cpu/cpu_overload.sh`（background）、`config/scripts/network/net_delay.sh`（sync）。

## 配置发现

`dcat` 通过 `/proc/self/exe` 解析自身路径，推导出**固定相对路径**：
`<binary_dir>/../config/demoncat.conf`。配置和脚本目录随二进制一同部署，无需环境变量、无需拷贝。

`--config <path>` 作为可选覆盖（测试/特殊场景）。

## 输出与退出码

stdout 输出 JSON。示例：

```json
{"status":"ok","op":"inject","uid":"rCPU_overload","data":{"pid":123,"record_id":1}}
{"status":"error","op":"inject","uid":"rX","error":{"code":4,"message":"uid not found"}}
```

退出码：`0` 成功 · `1` 运行错误 · `2` 解析错误 · `3` 预检/安全拒绝 · `4` 未找到。

## 测试

```sh
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

测试用 `mock_executor` 捕获 dcat 下发的命令串（不执行真实故障），表驱动断言覆盖
解析、注册表、安全预检、状态记录、自动清理、reaper 命令引号、各模块故障。

## 项目结构

```
CAT/
├── CMakeLists.txt
├── SPEC.md  DESIGN.md  README.md
├── src/
│   ├── main.c                  # 编排入口
│   ├── core/                   # cli config registry executor state safety output dispatch
│   └── injectors/              # 高级编译注入器扩展点（P0 留位）
├── third_party/cjson/
├── config/
│   ├── demoncat.conf           # 故障目录配置（18 条）
│   └── scripts/
│       ├── cpu/                # cpu_overload + cpu_core_offline
│       ├── network/            # 11 条网络故障
│       ├── process/            # 4 条进程故障（含 rPROC_exit inject-only）
│       └── storage/            # disk_write_overload
└── tests/                      # 12 个 CTest 用例
```

## 许可

见源文件头。cJSON 为 MIT 许可（vendored）。
