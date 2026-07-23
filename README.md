# DemonCAT (dcat)

> **DemonCAT**（简称 **dcat**）— Demon Computing Availability Tools
> Linux 计算故障注入工具：统一的命令面、预检护栏、状态跟踪；具体故障以**外部脚本 + 声明式配置**接入。

> 覆盖 CPU / 存储 / 网络 / 进程模块。加一个故障 = 加一个脚本 + 配置文件一行，**免重新编译**。

## 快速开始

```bash
# 编译
cmake -B build && cmake --build build

# 运行测试
ctest --test-dir build --output-on-failure

# 列出故障目录
./build/dcat list

# 注入 CPU 过载（4 核）
./build/dcat inject rCPU_overload --cores=4

# 查询活跃注入
./build/dcat query

# 清除故障
./build/dcat clean rCPU_overload --cores=4
```

## 命令格式

```
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

| 子命令 | 说明 | 示例 |
|---|---|---|
| `inject <uid> --p1=v1 ...` | 注入故障，同步阻塞执行 | `dcat inject rCPU_overload --cores=4` |
| `clean <uid> [--k1=v1 ...]` | 按参数匹配清除活跃注入 | `dcat clean rCPU_overload --cores=4` |
| `query [uid] [--k1=v1 ...]` | 无 uid：查全部活跃记录；有 uid：验证故障生效 | `dcat query` / `dcat query rCPU_overload` |
| `list` | 列出故障目录 | `dcat list` |

详细使用手册见 [MANUAL.md](MANUAL.md)，技术规格见 [SPEC.md](SPEC.md)，架构设计见 [DESIGN.md](DESIGN.md)。

## 当前故障目录（18 条）

| UID | 模块 | 必填 | 可选 | 说明 |
|---|---|---|---|---|
| `rCPU_overload` | cpu | cores | — | 多核 CPU 满载（yes 进程） |
| `rCPU_core_offline` | cpu | cores | — | CPU 核离线（sysfs） |
| `rDISK_write_overload` | storage | device | workers(默认4), size_mb(默认200) | 磁盘写压（dd 多实例） |
| `rNET_delay` | network | iface, delay_ms | — | 网络延迟（tc netem） |
| `rNET_loss` | network | iface, loss_pct | — | 网络丢包（tc netem） |
| `rNET_reorder` | network | iface, reorder_pct | — | 网络乱序（tc netem） |
| `rNET_down` | network | iface | — | 网卡 down（ip link） |
| `rNET_degrade` | network | iface | speed_mbps(默认10) | 网卡降速（ethtool） |
| `rNET_port_occupy` | network | port | protocol(默认tcp) | 端口占用（socket holder） |
| `rNET_service_stop` | network | service | — | 服务停止（systemctl） |
| `rNET_link_flap` | network | iface | cycle_sec(默认2), count(默认10) | 链路闪断（ip link 循环） |
| `rNET_bw_limit` | network | iface, rate_kbps | — | 带宽限制（tc tbf） |
| `rNET_jitter` | network | iface, delay_ms, jitter_ms | — | 延迟抖动（tc netem） |
| `rNET_tcp_loss` | network | port | direction(默认both) | TCP 丢包（iptables DROP） |
| `rPROC_exit` | process | pid | — | 进程退出（kill -9，不可恢复） |
| `rPROC_dstate` | process | count | — | D 状态进程（不可中断 IO） |
| `rPROC_hang` | process | pid | — | 进程挂起（SIGSTOP） |
| `rPROC_zstate` | process | count | — | 僵尸进程（fork+exit） |

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
