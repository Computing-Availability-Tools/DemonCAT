# DemonCAT (dcat)

> **DemonCAT**（简称 **dcat**）— Demon Computing Availability Tools，专注于计算故障注入工具。

覆盖 CPU / 存储 / 网络 / 进程 / NPU 模块，提供统一的命令面、预检护栏、状态跟踪；具体故障以**外部脚本 + 声明式配置**接入。

加一个故障 = 加一个脚本 + 配置文件一行，**免重新编译**。

## 依赖说明

极简 Linux 环境（最小安装/容器）可能不自带以下工具。运行 `scripts/install_deps.sh` 一键安装编译和运行依赖。

### 编译依赖

| 依赖 | 包名 (apt) | 包名 (yum) | 用途 |
|---|---|---|---|
| cmake ≥ 3.10 | `cmake` | `cmake` | 构建系统 |
| C 编译器 | `gcc` | `gcc` | 编译 dcat 二进制 |
| pthread | `libc6-dev` | `glibc-devel` | 状态锁 |
| dlopen | `libc6-dev` | `glibc-devel` | 动态插件加载 |

### 运行时依赖（按模块）

| 模块 | 工具 | 包名 (apt) | 包名 (yum) | 需要 root |
|---|---|---|---|---|
| **CPU** | `perl`, `taskset` | `perl`, `util-linux` | `perl`, `util-linux` | core_offline 需要 |
| **存储** | `dd` | `coreutils` | `coreutils` | — |
| **网络** | `tc`, `ip` | `iproute2` | `iproute` | ✅ |
| | `ethtool` | `ethtool` | `ethtool` | ✅ |
| | `iptables` | `iptables` | `iptables` | ✅ |
| | `systemctl` | `systemd` | `systemd` | ✅ |
| | `python3` | `python3` | `python3` | — |
| **进程** | `kill`, `perl` | `util-linux`, `perl` | `util-linux`, `perl` | 部分需要 |
| **NPU** | `hccn_tool` | — (Atlas 驱动自带) | — | ✅ |

> 无 NPU 硬件的环境可跳过 NPU 模块，不影响其他模块使用。

## 快速开始

```bash
# 1. 一键安装依赖（Debian/Ubuntu/RHEL/CentOS 自动识别）
bash scripts/install_deps.sh

# 2. 编译（8核并行加速）
cmake -B build && cmake --build build -j8

# 3. 运行测试
ctest --test-dir build --output-on-failure

# 4. 列出故障目录
./build/dcat list

# 5. 注入 CPU 过载（2 核）
./build/dcat inject rCPU_overload --cores=0,1

# 6. 查询故障是否生效
./build/dcat query rCPU_overload --cores=0,1

# 7. 清除故障
./build/dcat clean rCPU_overload --cores=0,1

# 查看帮助
./build/dcat --help
./build/dcat inject --help
./build/dcat inject rCPU_overload --help
```

## 命令格式

```
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

| 子命令 | 说明 | 示例 |
|---|---|---|
| `inject <uid> --p1=v1 ...` | 注入故障，同步阻塞执行 | `dcat inject rCPU_overload --cores=4` |
| `clean <uid> [--k1=v1 ...]` | 带参数：按参数匹配清除活跃注入；无参数：stateless 清该 uid 全部 `/tmp` 工件（state.json 丢失仍可清） | `dcat clean rCPU_overload --cores=4` / `dcat clean rCPU_overload` |
| `clean --all` | 对全部支持 clean 的故障 fan-out 无参 clean（stateless） | `dcat clean --all` |
| `query [uid] [--k1=v1 ...]` | 无 uid：查全部活跃记录；有 uid：验证故障生效 | `dcat query` / `dcat query rCPU_overload` |
| `list` | 列出故障目录 | `dcat list` |

详细使用手册见 [docs/user_manual.md](docs/user_manual.md)，技术规格见 [SPEC.md](SPEC.md)，架构设计见 [DESIGN.md](DESIGN.md)。

## 当前故障目录（36 条）

### CPU 模块（2 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rCPU_overload` | cores | — | CPU核心满载（支持多核心同时满载，perl纯用户态） |
| `rCPU_core_offline` | cores | — | CPU 核离线（sysfs） |

### 存储模块（1 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rDISK_write_overload` | device | workers(默认4), size_mb(默认200) | 磁盘写压（dd 多实例） |

### 网络模块（11 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rNET_delay` | iface, delay_ms | — | 网络延迟（tc netem） |
| `rNET_loss` | iface, loss_pct | — | 网络丢包（tc netem） |
| `rNET_reorder` | iface, reorder_pct | — | 网络乱序（tc netem） |
| `rNET_down` | iface | — | 网卡 down（ip link） |
| `rNET_degrade` | iface | speed_mbps(默认10) | 网卡降速（ethtool） |
| `rNET_port_occupy` | port | protocol(默认tcp) | 端口占用（socket holder） |
| `rNET_service_stop` | service | — | 服务停止（systemctl） |
| `rNET_link_flap` | iface | cycle_sec(默认2), count(默认10) | 链路闪断（ip link 循环） |
| `rNET_bw_limit` | iface, rate_kbps | — | 带宽限制（tc tbf） |
| `rNET_jitter` | iface, delay_ms, jitter_ms | — | 延迟抖动（tc netem） |
| `rNET_tcp_loss` | port | direction(默认both) | TCP 丢包（iptables DROP） |

### 进程模块（3 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rPROC_exit` | pid | — | 进程退出（kill -9，不可恢复，inject-only） |
| `rPROC_hang` | pid | — | 进程挂起（SIGSTOP） |
| `rPROC_zstate` | pid | — | 僵尸进程（kill 目标进程 → 僵尸，clean 杀父进程回收，不可恢复） |

### NPU 模块（19 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rNPU_link_down` | chip | — | RoCE 链路 down（-cfg recovery） |
| `rNPU_ip_change` | chip, address, netmask | — | RoCE IP 变更（sidecar 回放） |
| `rNPU_gw_change` | chip, gateway | — | RoCE 网关变更（sidecar 回放） |
| `rNPU_netdetect_change` | chip, address | — | Netdetect IP 变更（sidecar 回放） |
| `rNPU_arp_poison` | chip, dev, ip, mac | — | ARP 毒化（add wrong mac） |
| `rNPU_arp_del` | chip, dev, ip | — | ARP 条目删除（sidecar 回放） |
| `rNPU_route_add` | chip, address, netmask, gateway | — | 添加 RoCE 路由（del 清理） |
| `rNPU_route_del` | chip, address, netmask | — | 删除 RoCE 路由（sidecar 回放） |
| `rNPU_route_clear` | chip | — | 清空路由表（-cfg recovery） |
| `rNPU_iprule_add` | chip, dir, ip, table | — | 添加 ip rule（del 清理） |
| `rNPU_iprule_del` | chip, dir, ip | — | 删除 ip rule（sidecar 回放） |
| `rNPU_iproute_add` | chip, ip, ip_mask, via, dev, table | — | 添加 ip route（del 清理） |
| `rNPU_iproute_del` | chip, ip, ip_mask, table | — | 删除 ip route（sidecar 回放） |
| `rNPU_bw_limit` | chip, bw_limit | — | RoCE 带宽限速（设回 max） |
| `rNPU_mtu_mismatch` | chip, size | — | RoCE MTU 变更（sidecar 回放） |
| `rNPU_dscp_tc_change` | chip, dscp, tc | — | DSCP→TC 映射变更（sidecar 回放） |
| `rNPU_prio_tc_change` | chip, map | — | Prio→TC 映射变更（sidecar 回放） |
| `rNPU_pfc_change` | chip, bitmap | — | PFC 位图变更（sidecar 回放） |
| `rNPU_roce_port_change` | chip, port | — | RoCE UDP 端口变更（sidecar 回放） |

## 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 运行错误（脚本失败等） |
| 2 | 解析错误（命令格式不合法） |
| 3 | 预检拒绝（参数缺失/不合法/op 不支持） |
| 4 | 未找到（uid 不在目录中） |

## 技术栈

- C11（ISO/IEC 9899:2011），CMake 构建
- cJSON（vendored 单文件库）
- pthread（状态锁）
- INI 配置文件（`demoncat.conf`）
- 输出格式：JSON
