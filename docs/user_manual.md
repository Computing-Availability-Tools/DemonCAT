# DemonCAT 用户手册

> DemonCAT（`dcat`）—— Linux 计算故障注入工具。
> 覆盖 CPU / 存储 / 网络 / 进程 / NPU 五大模块，共 33 条故障。
> 完整规格见 [SPEC.md](../SPEC.md)，架构见 [DESIGN.md](../DESIGN.md)。

---

## 故障能力清单

| 模块 | 条数 | 故障范围 |
|---|:---:|---|
| CPU | 2 | 核满载（纯用户态）、核离线 |
| 存储 | 1 | 磁盘写压（dd 多实例） |
| 网络 | 11 | 延迟 / 丢包 / 乱序 / 网卡 down / 降速 / 端口占用 / 服务停止 / 链路闪断 / 带宽限制 / 抖动 / TCP 丢包 |
| 进程 | 3 | 进程退出 / 挂起 / 僵尸 |
| NPU | 16 | RoCE 链路 / IP / 网关 / ARP / 路由 / 策略路由 / 带宽 / MTU / DSCP / RoCE 端口 |
| **合计** | **33** | |

---

## 目录

- [故障能力清单](#故障能力清单)
- [通用约定：重注入与 --force](#通用约定重注入与---force)
- [第一章 CPU 模块](#第一章-cpu-模块2-条)
  - [1.1 rCPU_overload](#11-rcpu_overload) — 核满载
  - [1.2 rCPU_core_offline](#12-rcpu_core_offline) — 核离线
- [第二章 存储模块](#第二章-存储模块1-条)
  - [2.1 rDISK_write_overload](#21-rdisk_write_overload) — 磁盘写压
- [第三章 网络模块](#第三章-网络模块11-条)
  - [3.1 rNET_delay](#31-rnet_delay) — 网络延迟
  - [3.2 rNET_loss](#32-rnet_loss) — 网络丢包
  - [3.3 rNET_reorder](#33-rnet_reorder) — 网络乱序
  - [3.4 rNET_down](#34-rnet_down) — 网卡 down
  - [3.5 rNET_degrade](#35-rnet_degrade) — 网卡降速
  - [3.6 rNET_port_occupy](#36-rnet_port_occupy) — 端口占用
  - [3.7 rNET_service_stop](#37-rnet_service_stop) — 服务停止
  - [3.8 rNET_link_flap](#38-rnet_link_flap) — 链路闪断
  - [3.9 rNET_bw_limit](#39-rnet_bw_limit) — 带宽限制
  - [3.10 rNET_jitter](#310-rnet_jitter) — 延迟抖动
  - [3.11 rNET_tcp_loss](#311-rnet_tcp_loss) — TCP 丢包
- [第四章 进程模块](#第四章-进程模块3-条)
  - [4.1 rPROC_exit](#41-rproc_exit) — 进程退出
  - [4.2 rPROC_hang](#42-rproc_hang) — 进程挂起
  - [4.3 rPROC_zstate](#43-rproc_zstate) — 僵尸进程
- [第五章 NPU 模块](#第五章-npu-模块16-条)
  - [5.1 rNPU_link_down](#51-rnpu_link_down) — RoCE 链路 down
  - [5.2 rNPU_ip_change](#52-rnpu_ip_change) — RoCE IP 变更
  - [5.3 rNPU_gw_change](#53-rnpu_gw_change) — RoCE 网关变更
  - [5.4 rNPU_netdetect_change](#54-rnpu_netdetect_change) — Netdetect IP 变更
  - [5.5 rNPU_arp_poison](#55-rnpu_arp_poison) — ARP 毒化
  - [5.6 rNPU_arp_del](#56-rnpu_arp_del) — ARP 条目删除
  - [5.7 rNPU_route_add](#57-rnpu_route_add) — 添加 RoCE 路由
  - [5.8 rNPU_route_del](#58-rnpu_route_del) — 删除 RoCE 路由
  - [5.9 rNPU_iprule_add](#59-rnpu_iprule_add) — 添加 ip rule
  - [5.10 rNPU_iprule_del](#510-rnpu_iprule_del) — 删除 ip rule
  - [5.11 rNPU_iproute_add](#511-rnpu_iproute_add) — 添加 ip route
  - [5.12 rNPU_iproute_del](#512-rnpu_iproute_del) — 删除 ip route
  - [5.13 rNPU_bw_limit](#513-rnpu_bw_limit) — RoCE 带宽限速
  - [5.14 rNPU_mtu_mismatch](#514-rnpu_mtu_mismatch) — RoCE MTU 变更
  - [5.15 rNPU_dscp_tc_change](#515-rnpu_dscp_tc_change) — DSCP→TC 映射变更
  - [5.16 rNPU_roce_port_change](#516-rnpu_roce_port_change) — RoCE UDP 端口变更

---

## 通用约定：重注入与 --force

dcat 对**同一资源的重复注入默认拒绝**（退出码 5），需显式 `--force` 才原子替换。这避免意外的故障叠加/资源冲突（如两次 CPU 满载抢核、两个 tc qdisc 打架）。

- **资源键**：各故障的 `clean_required` 参数（见每章参数表）。`cores` 走核集交集，其余走精确等值。
- 同资源（同 iface / 重叠核 / 同 pid）重注入 → **拒绝**；加 `--force` → 先清旧再注新（原子替换）。
- 不同资源（不同 iface / 不重叠核段）→ 并发注入 OK，互不影响。
- inject-only 故障（如 `rPROC_exit`）不写 state，可重复 inject。
- `--force` 仅 inject 生效；clean/query/list 上忽略。`--force=x`（带值）报错。

```bash
dcat inject rCPU_overload --cores=0,1
dcat inject rCPU_overload --cores=0,1 --force      # 替换（否则拒绝）
dcat inject rCPU_overload --cores=2,3               # 不同核，并发 OK
dcat inject rNET_delay --iface=eth0 --delay_ms=100
dcat inject rNET_delay --iface=eth0 --delay_ms=200 --force   # 替换
```

> **BREAKING**：相对旧版，CPU 同核/重叠核重注入从"幂等共存"改为"默认拒绝"。重注入请加 `--force`。

---

## 通用约定：clean --all 与 stateless clean

除 `clean <uid> --params`（按参数匹配 state 记录逐条清理）外，clean 还支持两种 **stateless** 形式，不依赖 `state.json`，脚本自行 glob `/tmp` 工件清理：

- **`dcat clean <uid>`（无参）**：清该 uid 全部 `/tmp/dcat-<uid>-*` 工件（PID 文件、sidecar 临时状态文件、.bak 备份），不查 state。`state.json` 丢失/损坏时仍可用。
- **`dcat clean --all`**：对全部支持 clean 的故障 fan-out 无参 clean，聚合返回 `{uid,status}` 数组。

```bash
dcat clean rCPU_overload              # stateless：清该 uid 全部 cpu_overload pidfile
dcat clean rNET_loss                  # stateless：清该 uid 全部网卡 netem
dcat clean --all                      # 清全部故障（stateless，state.json 丢失仍可清）
```

> **说明**：NPU 中 clean 需 `chip` + 标识参数（如 arp/route/iprule）的故障（无 /tmp 工件可枚举其标识）在 `clean --all` 下报 "no active injection" 退出 0，**不实际清理**——此类故障的 stateless 清理需带参（`clean <uid> --chip=N [--key=...]`）或依赖完好的 state.json。
> 脚本 clean 输出约定**仅一行汇总**（循环体内不 echo），因 executor 对 stdout 管道单次 read 后即关闭，多行会触发 SIGPIPE 误判失败。

---

## 第一章 CPU 模块（2 条）

### 1.1 rCPU_overload — 核满载（perl 纯用户态）

**UID**: `rCPU_overload`

**描述**: 通过 `taskset` 绑定到指定 CPU 核运行纯用户态死循环（`perl -e '1 while 1'`），使指定核 100% 用户态满载。

**实现原理**: 
- **inject**: 从 `DCAT_PARAM_CORES` 取必填的 cores 规格（支持 `0,2,4` / `0-3` / `0-3,7` 等混合格式，由 `parse_cores` 展开为单核号列表）。对每个核，用 `taskset -c <n>` 绑定启动 `perl -e '1 while 1'`（纯用户态死循环，无系统调用开销），后台运行并重定向到 `/dev/null`；若系统无 perl，自动回退为 `yes`（会引入约 60% 系统调用开销）。将所有子进程 pid 写入 pidfile `/tmp/dcat-rCPU_overload-${spec}.pid`（spec 为原始参数串），输出注入结果。
- **clean**: 读取 pidfile，逐个 `kill` 进程并删除 pidfile；若 pidfile 不存在则报错并 `exit 1`。
- **query**: 用 `pgrep -f 'perl -e'` 与 `pgrep -x yes` 统计系统中 burn 进程总数，打印 per-core CPU（mpstat）及匹配进程的 `pid/%cpu/psr/cmd` 详情；进程总数 > 0 时返回成功（exit 0），否则失败。**无 `--cores` 参数时查询全部在线核**（读 `/sys/devices/system/cpu/online`，形如 `0-27`）；带 `--cores=0,1` 则只查指定核。

**使用示例**:
```bash
dcat inject rCPU_overload --cores=0,1
dcat query  rCPU_overload                 # 无参 = 查全部在线核
dcat query  rCPU_overload --cores=0,1     # 只查 0,1 号核
dcat clean  rCPU_overload --cores=0,1
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| cores | inject/clean 必填；query 可选 | 核号列表 | 支持 `"0,2,4"` 或 `"0-3"` 或 `"0-3,7"` 格式；query 缺省时查全部在线核 |

**危险等级**: 中 — 指定核用户态 100% 满载，影响该核上其他任务调度。

**补充说明**: 依赖 `taskset`（util-linux）；优先使用 perl（纯用户态），无 perl 时回退到 yes。query 统计的是全系统 perl/yes 进程数。clean 必须传入与 inject 完全相同的 cores 规格。

---

### 1.2 rCPU_core_offline — 核离线（sysfs）

**UID**: `rCPU_core_offline`

**描述**: 通过 sysfs 将指定 CPU 核下线（`echo 0 > /sys/devices/system/cpu/cpu<N>/online`），直接减少系统可用算力。

**实现原理**: 
- **inject**: 对每个核，检查 `/sys/devices/system/cpu/cpu<N>/online` 是否可写；不可写（如 cpu0）则跳过并告警，可写则 `echo 0` 下线。实际下线成功的核列表写入 sidecar `/tmp/dcat-rCPU_core_offline.list`。
- **clean**: 读取 sidecar 中的核列表，对每个核 `echo 1` 重新上线，删除 sidecar。
- **query**: 读取每个请求核的 online 值，打印 `core/online/status` 表格；存在 OFFLINE 核即返回成功。

**使用示例**:
```bash
dcat inject rCPU_core_offline --cores=2,3
dcat query rCPU_core_offline --cores=2,3
dcat clean rCPU_core_offline --cores=2,3
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| cores | 必填 | 核号列表 | 支持 `"0,2,4"` 或 `"0-3"` 格式；cpu0 通常不可下线，自动跳过 |

**危险等级**: 高 — 直接将 CPU 核下线，减少系统可用算力；下线多核可能触发调度器重平衡与 NUMA 重排。

**补充说明**: 需要 root 权限写 sysfs；依赖内核 `CONFIG_HOTPLUG_CPU` 支持。部分虚拟化/容器环境不支持核下线。clean 仅恢复实际下线成功的核。核离线在部分内核版本上可能引起系统宕机，请谨慎使用；另外 **cpu0（0 号核心）在很多内核/平台上无法离线**，脚本会自动跳过并告警，这是正常现象，不代表故障注入失败。

---

## 第二章 存储模块（1 条）

### 2.1 rDISK_write_overload — 磁盘写压（dd 多实例）

**UID**: `rDISK_write_overload`

**描述**: 通过多路 `dd` 进程持续向目标设备写入数据（`if=/dev/zero` + `fdatasync`），制造磁盘写 IO 过载。

**实现原理**: 
- **inject**: 启动 workers 个后台循环，每轮执行 `dd if=/dev/zero of=${target}.${i} bs=1M count=$size conv=fdatasync`（强制落盘），失败则 `sleep 1` 重试。将所有 worker pid 写入 pidfile。
- **clean**: 读取 pidfile，逐个 kill worker 进程，删除 pidfile 和临时文件（`dcat.stress.*` / `dcat.write.*`）。
- **query**: 用 `pgrep -af 'dd if=/dev/zero'` 统计 dd 进程数，存在则打印进程列表与临时文件。

**使用示例**:
```bash
dcat inject rDISK_write_overload --device=/data --workers=8 --size_mb=500
dcat query rDISK_write_overload --device=/data
dcat clean rDISK_write_overload --device=/data
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| device | 必填 | 设备/目录路径 | 写入目标；目录则在其下写 `dcat.stress.*`，非目录则写 `/tmp/dcat.write.*` |
| workers | 可选 | 整数 | 并发 dd 写进程数，默认 `4` |
| size_mb | 可选 | 整数 | 单次 dd 写入块大小（MB），默认 `200` |

**危险等级**: 中 — 持续写盘占用 IO 带宽并消耗磁盘空间，拖慢同盘其他 IO 任务；长期运行可能写满磁盘。

**补充说明**: clean 必须传入与 inject 相同的 device（pidfile 按路径命名）。建议配合 `size_mb` 控制单轮写入量以避免过快写满磁盘。

---

## 第三章 网络模块（11 条）

本章涵盖 DemonCAT 网络故障注入模块的全部 11 条故障规则。网络模块通过 `tc`（Traffic Control）、`ip`、`iptables`、`systemctl` 及 Python socket 等手段，模拟延迟、丢包、乱序、带宽限制、链路中断、端口占用、服务停止、链路抖动等多种网络异常场景。

所有故障均支持 `inject`（注入）、`clean`（清理）、`query`（查询）三个操作。注入时通过 sidecar 临时状态文件（`/tmp/dcat-rNET_*`，用于记录注入前的原始值，便于 clean 时恢复）或 PID 文件记录状态，便于后续清理与查询。

---

### 3.1 rNET_delay — 网络延迟（tc netem）

**UID**: `rNET_delay`

**描述**: 通过 `tc netem` 在指定网卡出向流量上注入固定网络延迟。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem delay <delay_ms>ms`，在网卡根队列上挂载 netem qdisc 并设置固定延迟；将网卡名写入 sidecar 文件 `/tmp/dcat-rNET_delay-<iface>.sidecar`。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `netem.*delay` 匹配判断延迟规则是否生效，匹配则退出码 0，否则退出码 1。

**使用示例**:
```bash
dcat inject rNET_delay --iface=eth0 --delay_ms=100
dcat query rNET_delay --iface=eth0
dcat clean rNET_delay --iface=eth0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33、enp0s3 |
| delay_ms | 必填 | 整数 | 延迟毫秒数，如 100 表示 100ms 延迟 |

**危险等级**: 低 — 仅增加网络延迟，不中断连接，不影响其他网卡。但大延迟值可能导致依赖低延迟的应用（如心跳、实时通信）超时。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块；同一网卡若已有 qdisc 或注入了其他 qdisc 故障，则 `tc qdisc add` 会失败，需先 clean 或手动删除；clean 操作会删除网卡上所有 root qdisc，注意与手动配置的冲突。

---

### 3.2 rNET_loss — 网络丢包（tc netem）

**UID**: `rNET_loss`

**描述**: 通过 `tc netem` 在指定网卡出向流量上注入随机丢包。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem loss random <loss_pct>%`，在网卡根队列上挂载 netem qdisc 并设置随机丢包百分比；将网卡名写入 sidecar 文件 `/tmp/dcat-rNET_loss-<iface>.sidecar`。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `netem.*loss` 匹配判断丢包规则是否生效。

**使用示例**:
```bash
dcat inject rNET_loss --iface=eth0 --loss_pct=10
dcat query rNET_loss --iface=eth0
dcat clean rNET_loss --iface=eth0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33 |
| loss_pct | 必填 | 整数 | 丢包百分比，范围 0–100，如 10 表示 10% 丢包率 |

**危险等级**: 中 — 丢包率过高会导致 TCP 连接重传甚至超时断开，UDP 应用丢数据，影响所有经过该网卡的流量。建议测试时从低值（1–5%）开始。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块；同一网卡已有 qdisc 或注入了其他 qdisc 故障时 `tc qdisc add` 会失败；clean 会删除网卡上所有 root qdisc。

---

### 3.3 rNET_reorder — 网络乱序（tc netem）

**UID**: `rNET_reorder`

**描述**: 通过 `tc netem` 在指定网卡出向流量上注入包乱序（reorder）。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem delay 10ms reorder <reorder_pct>% 50%`，在网卡根队列上挂载 netem qdisc，内含固定 10ms 延迟作为乱序基准，并按指定百分比和 50% 相关度（correlation）触发包重排；将网卡名写入 sidecar 文件 `/tmp/dcat-rNET_reorder-<iface>.sidecar`。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `netem.*reorder` 匹配判断乱序规则是否生效。

**使用示例**:
```bash
dcat inject rNET_reorder --iface=eth0 --reorder_pct=25
dcat query rNET_reorder --iface=eth0
dcat clean rNET_reorder --iface=eth0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33 |
| reorder_pct | 必填 | 整数 | 乱序百分比，范围 0–100，如 25 表示 25% 的包会被乱序 |

**危险等级**: 低 — 主要影响 TCP 性能（触发乱序检测与快速重传），通常不中断连接。注意 netem reorder 需配合 delay 参数，脚本内部固定为 10ms 延迟和 50% correlation，不可通过参数修改。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块；乱序的 delay 基准（10ms）和 correlation（50%）为脚本硬编码值，无法通过参数调整；同一网卡已有 qdisc 或注入了其他 qdisc 故障时注入会失败。

---

### 3.4 rNET_down — 网卡 down（ip link）

**UID**: `rNET_down`

**描述**: 通过 `ip link set down` 将指定网卡置为 DOWN 状态，模拟网卡链路中断。

**实现原理**: `inject` 执行 `ip link set dev <iface> down`，将网卡链路状态置为 DOWN；将网卡名写入 sidecar 文件 `/tmp/dcat-rNET_down-<iface>.sidecar`。`clean` 从 sidecar 读取网卡名，执行 `ip link set dev <iface> up` 恢复链路并删除 sidecar 文件。`query` 执行 `ip -o link show dev <iface>`，通过匹配 `state DOWN` 判断网卡是否处于 DOWN 状态。

**使用示例**:
```bash
dcat inject rNET_down --iface=eth0
dcat query rNET_down --iface=eth0
dcat clean rNET_down --iface=eth0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33。切勿对管理网卡或 SSH 依赖的网卡执行，否则可能导致失联 |

**危险等级**: 高 — 网卡 DOWN 后该网卡所有 IP 不可达，所有经过该网卡的连接立即中断。若目标为管理网卡或 SSH 所用网卡，将导致远程连接丢失，需通过带外管理或物理终端恢复。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `ip` 命令（iproute2 包）；严禁对管理网卡/SSH 网卡执行；clean 仅恢复链路 UP 状态，不恢复 IP 地址/DHCP/路由等上层配置，若网卡依赖 DHCP 可能需要额外等待或手动 `dhclient`。

---

### 3.5 rNET_degrade — 网卡降速（tc tbf）

**UID**: `rNET_degrade`

**描述**: 通过 `tc tbf` 限速模拟网卡性能降级。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root tbf rate <speed_mbps>mbit burst <speed_mbps>kbit latency 400ms`，将网卡出向带宽限制为指定速率（默认 10Mbps）；将 `iface speed` 写入 sidecar 文件 `/tmp/dcat-rNET_degrade-<iface>.sidecar`。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除限速规则，删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，检查是否存在 tbf 规则。

**使用示例**:
```bash
dcat inject rNET_degrade --iface=eth0 --speed_mbps=10
dcat query rNET_degrade --iface=eth0 --speed_mbps=10
dcat clean rNET_degrade --iface=eth0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33、dummy 网卡 |
| speed_mbps | 可选 | 整数 | 目标速率（Mbps），默认 10。需 ≥ 1 |

**危险等级**: 中 — 限速后网卡带宽大幅降低（如从 1000Mbps 降至 10Mbps），大流量场景下可能导致拥塞、丢包和应用超时。

**补充说明**: 需要 root 权限；依赖 `tc` 命令；与 `rNET_bw_limit` 同为 tc tbf 机制但语义不同（degrade=模拟慢网卡，bw_limit=模拟带宽拥塞）；dummy 虚拟网卡和真实物理网卡均可测试。

---

### 3.6 rNET_port_occupy — 端口占用（socket holder）

**UID**: `rNET_port_occupy`

**描述**: 通过 Python socket 占用指定 TCP/UDP 端口，阻止其他进程绑定该端口，模拟端口冲突。

**实现原理**: `inject` 使用 `python3` 创建 socket，设置 `SO_REUSEADDR`，绑定 `0.0.0.0:<port>`；TCP 模式下调用 `listen(1)`，之后进入 `sleep(3600)` 循环保持占用；以后台进程运行，将 PID 写入 PID 文件 `/tmp/dcat-rNET_port_occupy-<port>.pid`。`clean` 从 PID 文件读取进程号，执行 `kill` 终止占用进程并删除 PID 文件；若 PID 文件不存在则报错退出。`query` 优先使用 `ss -tulnp`（回退 `netstat -tulnp`）列出监听端口，通过正则 `[:.]<port>` 匹配判断端口是否被占用。

**使用示例**:
```bash
dcat inject rNET_port_occupy --port=8080 --protocol=tcp
dcat query rNET_port_occupy --port=8080
dcat clean rNET_port_occupy --port=8080
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| port | 必填 | 整数 | 目标端口号，范围 1–65535。占用 1024 以下端口需要 root 权限 |
| protocol | 可选 | 字符串 | 协议类型，`tcp` 或 `udp`，默认 `tcp` |

**危险等级**: 低 — 仅占用单个端口，不影响其他端口的网络通信。但若占用的是关键服务端口（如 80、443、22），则该服务无法启动。占用系统端口（<1024）需要 root。

**补充说明**: 依赖 `python3`，未安装时报错退出；query 依赖 `ss` 或 `netstat`（至少其一）；不需要 `CAP_NET_ADMIN`，但绑定 <1024 端口需要 root 或 `CAP_NET_BIND_SERVICE`；设置了 `SO_REUSEADDR`，但 TCP `listen` 模式仍会阻止其他进程 `bind` 同端口（TIME_WAIT 场景除外）；进程以 nohup 风格后台运行，系统重启后自动释放。

---

### 3.7 rNET_service_stop — 服务停止（systemctl）

**UID**: `rNET_service_stop`

**描述**: 通过 `systemctl stop` 或 `pkill` 停止指定网络服务，模拟服务级网络故障。

**实现原理**: `inject` 优先检测 `systemctl` 是否可用：若可用则执行 `systemctl stop <service>`，否则执行 `pkill -x <service>` 按进程名精确杀停；将服务名写入 sidecar 文件 `/tmp/dcat-rNET_service_stop-<service>.sidecar`。`clean` 从 sidecar 读取服务名：若 `systemctl` 可用则执行 `systemctl start <service>`，否则执行 `service <service> start`（容错），删除 sidecar 文件。`query` 若 `systemctl` 可用则执行 `systemctl is-active <service>`，当状态为 `inactive`、`failed` 或 `deactivating` 时判定服务已停止（退出码 0），否则退出码 1；无 systemctl 时通过 `pgrep -x <service>` 判断，无进程则退出码 0。

**使用示例**:
```bash
dcat inject rNET_service_stop --service=nginx
dcat query rNET_service_stop --service=nginx
dcat clean rNET_service_stop --service=nginx
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| service | 必填 | 字符串 | 服务名称。systemd 环境下为 unit 名（如 nginx、sshd、httpd）；非 systemd 环境下为进程名（用于 pkill -x） |

**危险等级**: 高 — 停止关键网络服务（如 sshd、nginx、kubelet）会导致远程管理中断、Web 服务不可用、集群节点异常等。clean 通过 systemctl start 或 service start 恢复，但若服务配置异常可能无法成功启动。

**补充说明**: 需要 root 权限；优先使用 `systemctl`（systemd 环境），回退 `pkill`/`service`（非 systemd 环境）；严禁停止 sshd 等管理服务以免失联；pkill -x 为精确匹配进程名，若服务主进程名与 unit 名不同可能无效；clean 的非 systemd 路径 `service start` 带 `|| true` 容错，若服务启动失败不会报错，需手动确认。

---

### 3.8 rNET_link_flap — 链路闪断（ip link 循环）

**UID**: `rNET_link_flap`

**描述**: 通过后台循环执行 `ip link set down/up`，模拟网卡链路反复抖动（link flap）。

**实现原理**: `inject` 启动后台子 shell，循环执行 `ip link set dev <iface> down` → `sleep <cycle_sec>` → `ip link set dev <iface> up` → `sleep <cycle_sec>`，重复 `<count>` 次后自动结束；将子 shell 的 PID 写入 PID 文件 `/tmp/dcat-rNET_link_flap-<iface>.pid`。`clean` 从 PID 文件读取进程号并 `kill` 终止循环，删除 PID 文件，并确保网卡执行 `ip link set dev <iface> up` 恢复 UP 状态。`query` 检查 PID 文件是否存在且对应进程仍存活（`kill -0`），存活则退出码 0，否则退出码 1。

**使用示例**:
```bash
dcat inject rNET_link_flap --iface=eth0 --cycle_sec=2 --count=10
dcat query rNET_link_flap --iface=eth0
dcat clean rNET_link_flap --iface=eth0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33 |
| cycle_sec | 可选 | 整数 | 每次 down/up 的间隔秒数，默认 2 |
| count | 可选 | 整数 | 抖动循环次数，默认 10 |

**危险等级**: 高 — 链路反复 down/up 会导致该网卡上所有连接频繁中断重建，可能触发上层协议重连、ARP 刷新、HA 脑裂、负载均衡剔除节点等连锁反应。严禁对管理网卡执行。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `ip` 命令（iproute2）；后台进程以子 shell 形式运行，系统重启后自动停止；循环结束后网卡最终为 UP 状态（最后一次循环为 up + sleep），但 clean 仍会强制确保 UP；严禁对管理/SSH 网卡执行；query 仅判断后台进程是否存活，不判断当前链路状态。

---

### 3.9 rNET_bw_limit — 带宽限制（tc tbf）

**UID**: `rNET_bw_limit`

**描述**: 通过 `tc tbf`（Token Bucket Filter）在指定网卡上注入带宽限速。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root tbf rate <rate_kbps>kbit burst 32kbit latency 400ms`，在网卡根队列上挂载 TBF qdisc，按指定速率限速（burst 32kbit，延迟上限 400ms）；将网卡名写入 sidecar 文件 `/tmp/dcat-rNET_bw_limit-<iface>.sidecar`。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `qdisc tbf` 匹配判断限速规则是否生效。

**使用示例**:
```bash
dcat inject rNET_bw_limit --iface=eth0 --rate_kbps=1024
dcat query rNET_bw_limit --iface=eth0
dcat clean rNET_bw_limit --iface=eth0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33 |
| rate_kbps | 必填 | 整数 | 限速速率（KB/s），脚本内部转换为 kbit。如 1024 表示约 1MB/s（8Mbit/s）带宽上限 |

**危险等级**: 低 — 仅限制出向带宽，不中断连接。低速率值会导致大文件传输、视频流等高带宽应用明显卡顿或超时。burst 和 latency 为固定值（32kbit / 400ms），极端低速率下可能不够精细。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_tbf` 模块；同一网卡已有 qdisc 或注入了其他 qdisc 故障时 `tc qdisc add` 会失败；clean 会删除网卡上所有 root qdisc；TBF 为出向限速，入向不限速。

---

### 3.10 rNET_jitter — 延迟抖动（tc netem）

**UID**: `rNET_jitter`

**描述**: 通过 `tc netem` 在指定网卡出向流量上注入延迟抖动（delay + jitter）。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem delay <delay_ms>ms <jitter_ms>ms`，在网卡根队列上挂载 netem qdisc，设置基础延迟及附加抖动范围（netem 默认使用正态分布抖动）；将网卡名写入 sidecar 文件 `/tmp/dcat-rNET_jitter-<iface>.sidecar`。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `netem.*delay [0-9]+[a-z]* [0-9]+[a-z]*` 匹配两个数值（delay + jitter），判断抖动规则是否生效。

**使用示例**:
```bash
dcat inject rNET_jitter --iface=eth0 --delay_ms=100 --jitter_ms=20
dcat query rNET_jitter --iface=eth0
dcat clean rNET_jitter --iface=eth0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33 |
| delay_ms | 必填 | 整数 | 基础延迟毫秒数，如 100 表示 100ms 基础延迟 |
| jitter_ms | 必填 | 整数 | 抖动范围毫秒数，如 20 表示 ±20ms 抖动。实际延迟在 [delay-jitter, delay+jitter] 范围内波动 |

**危险等级**: 低 — 主要影响实时音视频、在线游戏等对抖动敏感的应用，通常不中断 TCP 连接。大 jitter 值可能导致 TCP 超时重传。注意 delay 与 jitter 均作用于出向流量。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块；同一网卡已有 qdisc 或注入了其他 qdisc 故障时 `tc qdisc add` 会失败；clean 会删除网卡上所有 root qdisc；query 的正则要求 delay 行有两个数值（delay + jitter），仅有一个数值的纯延迟规则不会匹配。

---

### 3.11 rNET_tcp_loss — TCP 丢包（iptables DROP）

**UID**: `rNET_tcp_loss`

**描述**: 通过 `iptables DROP` 规则在指定端口上注入 TCP 包丢弃，模拟端口级 TCP 丢包。

**实现原理**: `inject` 根据 `direction` 参数在 iptables 中插入 DROP 规则：`in` 方向执行 `iptables -I INPUT -p tcp --dport <port> -j DROP`（丢弃入站目标端口包）；`out` 方向执行 `iptables -I OUTPUT -p tcp --sport <port> -j DROP`（丢弃出站源端口包）；`both` 方向同时插入两条规则；将 `port dir` 写入 sidecar 文件 `/tmp/dcat-rNET_tcp_loss-<port>.rule`。`clean` 从 sidecar 读取 `port dir`（回退默认 `both`），执行 `iptables -D INPUT ...` 和/或 `iptables -D OUTPUT ...` 删除对应规则，删除 sidecar 文件。`query` 根据 `direction` 执行 `iptables -L INPUT -n` 和/或 `iptables -L OUTPUT -n`，通过正则 `DROP.*dpt:<port>`（INPUT）和 `DROP.*spt:<port>`（OUTPUT）匹配判断规则是否存在，任一匹配则退出码 0。

**使用示例**:
```bash
dcat inject rNET_tcp_loss --port=8080 --direction=both
dcat query rNET_tcp_loss --port=8080 --direction=both
dcat clean rNET_tcp_loss --port=8080 --direction=both
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| port | 必填 | 整数 | 目标 TCP 端口号，范围 1–65535 |
| direction | 可选 | 字符串 | 丢包方向：`in`（入向）、`out`（出向）、`both`（双向），默认 `both` |

**危险等级**: 高 — DROP 规则会导致该端口上所有 TCP 连接的包被静默丢弃，新建连接无法建立、已有连接超时断开。`both` 方向影响最大。注意此规则在 iptables 层面生效，影响所有协议栈上层，且重启后规则不自动清除。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `iptables` 命令（传统 iptables，非 nftables）；clean 通过 `-D` 删除规则，若规则已被手动删除或顺序变化，`-D` 可能静默失败；sidecar 文件后缀为 `.rule`（非 `.sidecar`）；`direction=both` 时 inject 只要任一方向插入失败即整体报错退出；query 的 `direction` 参数需与 inject 时一致才能正确匹配；防火墙后端为 nftables 时 `iptables` 命令可能行为不同，需确认兼容性。

## 第四章 进程模块（3 条）

### 4.1 rPROC_exit — 进程退出（kill -9，inject-only）

**UID**: `rPROC_exit`

**描述**: 通过 `kill -9`（SIGKILL）强制终止目标进程，操作不可逆。

**实现原理**: inject 对目标 PID 发送 `kill -9`，进程被立即终止且无法恢复。本故障为 inject-only（`supported_ops = inject`），不支持 clean/query。脚本内含防御性 clean/query 分支但不作为官方支持的操作。

**使用示例**:
```bash
dcat inject rPROC_exit --pid=12345
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| pid | 必填 | 正整数 | 目标进程 PID |

**危险等级**: 高 — 进程被 `kill -9` 终止，不可恢复。

**补充说明**: inject-only 故障，不支持 clean/query（dcat 在 precheck 阶段拒绝，退出码 3）。需具备对目标进程发送信号的权限。

---



### 4.2 rPROC_hang — 进程挂起（SIGSTOP）

**UID**: `rPROC_hang`

**描述**: 对目标进程发送 `SIGSTOP` 使其挂起暂停，clean 发送 `SIGCONT` 恢复（可逆）。

**实现原理**: 
- **inject**: 对目标 PID 执行 `kill -STOP`，进程被暂停（状态变为 T），将 PID 写入 sidecar。
- **clean**: 从 sidecar 或参数取 PID，执行 `kill -CONT` 恢复进程运行，删除 sidecar。
- **query**: 通过 `kill -0` 确认进程存在，读取 `/proc/$pid/status` 的 `State:` 字段，状态以 `T` 开头则 exit 0。

**使用示例**:
```bash
dcat inject rPROC_hang --pid=12345
dcat query rPROC_hang --pid=12345
dcat clean rPROC_hang --pid=12345
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| pid | 必填 | 正整数 | 目标进程 PID（clean/query 可省略，从 sidecar 恢复） |

**危险等级**: 中 — 进程被暂停但可由 `SIGCONT` 恢复，可逆。

**补充说明**: 可逆故障，STOP/CONT 成对出现。clean 时可不带参数（从 sidecar 自动恢复 PID）。query 依赖 `/proc` 文件系统，仅 Linux 有效。

> **适用对象**：rPROC_hang 适用于**非终端控制的后台进程**（守护进程/工作进程/服务/`sleep`/构建任务等不持有控制终端的进程）。交互式终端程序（`top`/`vim`/`less`/`htop`）被 SIGSTOP 后，shell 作业控制会回收终端，clean 的 SIGCONT 使其在后台恢复→试图读终端→被终端驱动以 `SIGTTIN` 再次停止，故无法仅靠 `kill -CONT` 恢复（需 shell `fg` 重回前台进程组）。对交互终端程序的"挂起"用 shell 作业控制（Ctrl+Z / `fg`）更合适。

---

### 4.3 rPROC_zstate — 僵尸进程（kill 目标 → 僵尸）

**UID**: `rPROC_zstate`

**描述**: 将指定进程 kill 后变为僵尸进程（进程退出但父进程未调用 wait 回收，残留为 Z 状态）。

**实现原理**: 
- **inject**: 读取 `DCAT_PARAM_PID` 获取目标进程 PID，记录其父进程 PID（PPID）到 sidecar 文件，然后 `kill -9` 目标进程。进程退出后，如果父进程没有调用 wait 回收，则成为僵尸进程（Z 状态）。如果父进程立即回收，则僵尸不会持续存在（此为正常现象）。
- **clean**: 从 sidecar 读取目标 PID 和父进程 PID。如果僵尸仍存在，kill 父进程使僵尸 reparent 到 init（PID 1），init 自动回收僵尸。如果僵尸已被父进程回收，则无需操作。
- **query**: 检查目标 PID 的 `/proc/<pid>/status` 中 State 是否为 Z（僵尸）。是则返回 confirmed:true，否则返回 confirmed:false。

**使用示例**:
```bash
dcat inject rPROC_zstate --pid=12345
dcat query rPROC_zstate --pid=12345
dcat clean rPROC_zstate --pid=12345
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| pid | 必填 | 正整数 | 目标进程 PID |

**危险等级**: 中 — 会 kill 目标进程和其父进程，操作不可逆。clean 后目标进程已死，需手动重启。

**补充说明**: inject 后如果父进程立即回收子进程，则僵尸不会持续（这是正常行为，说明父进程实现良好）。clean 通过杀父进程强制 reparent 到 init 回收僵尸——如果父进程是关键服务，kill 父进程可能影响其他子进程。clean 后目标进程和父进程均已终止，无法自动恢复，需手动重启相关进程。

---

## 第五章 NPU 模块（16 条）

NPU 模块面向华为 Atlas 系列 NPU 芯片，通过 `hccn_tool` 对 RoCE 网口注入连通性、路由、性能与配置类故障。所有脚本共享 `_common.sh`，提供 `npu_check_env`（校验 hccn_tool）及 sidecar 读写原语（`/tmp/dcat-<uid>-<chip>.bak`）。

> ⚠️ **实机必读**：本章示例中的 `chip`、`dev`、`gateway`、各网段地址均为**机器相关**值——每台机器的 NPU IP、网关、网口名、已用网段都不一样，直接照抄大概率失败。**注入前必须先按下方「④ 前置参数查询」查出目标机器实际值，再填入各 fault 参数**。下文中 `10.30.12.x / 网关 10.30.12.254 / eth2 / 芯片 2` 是一台 Atlas 8 卡机器的示例拓扑值，仅用于演示查询与换算过程。

### 0 前置准备：实机参数查询与调整

所有 NPU 用例注入前，按下列步骤确认目标机器的实际参数。命令中 `chip` 用目标芯片号（示例取 2，请替换成可用芯片号）。

**① 确认可注入的芯片与 RoCE 网口名 `dev`**

```bash
npu-smi info                      # 查看目标机 NPU 拓扑，确认 0-7 内可用的芯片
hccn_tool -i 2 -status -g          # 查询芯片 2 的网口名，输出 "Settings for eth2:" → dev 为 eth2（示例机）
```

> **关键**：`hccn_tool` 的 `dev` 是 **NPU 内部网卡名**（示例机为 `eth2`，不同机器可能是 `eth0`/`eth2` 等），不是 Linux 系统接口名（如 `enp125s0f1`）。用 `hccn_tool -i <chip> -status -g` 可查询，输出首行 `Settings for ethX:` 中的 `ethX` 即为该芯片的 dev 值；各芯片 dev 需逐一确认（不同芯片可能不同）。**注入前务必用该命令确认目标芯片的真实 dev**，不要照抄示例的 eth2。

**② 查询 NPU IP 与掩码（确定网段）**

```bash
hccn_tool -i 2 -ip -g
# 示例输出: ipaddr:10.30.12.9 netmask:255.255.255.0  → 网段 10.30.12.0/24
```

**③ 查询当前网关**

```bash
hccn_tool -i 2 -gateway -g
# 示例输出 10.30.12.254（同机芯片 0/1/3 也多为 .254）
```

> 所有涉及 `gateway`/`via` 的注入（gw_change、route_add、iproute_add），该值**必须与 NPU IP 同网段**，否则 `hccn_tool` 报 `segment doesn't match`。若查询输出 `none`（未设网关），注入前需先 `hccn_tool -i <chip> -gateway -s gateway <同网段gw>` 预设一个，或接受 clean 时跳过恢复。

**④ 查询路由 / ARP / ip rule / ip route / MTU（判断存量与前提条件）**

```bash
hccn_tool -i 2 -route -g                       # 路由表（route_add/route_del 用）
hccn_tool -i 2 -arp -g                         # ARP 表（arp_poison/arp_del 用）
hccn_tool -i 2 -ip_rule -g                     # 策略路由规则（iprule_add/del 用）
hccn_tool -i 2 -ip_route -g table 100          # 指定路由表的 ip route（iproute_add/del 用）
hccn_tool -i 2 -mtu -g                         # 当前 MTU（mtu_mismatch 用）
```

**⑤ 测试网段选择原则**

- `route_add/route_del/iproute_add/iproute_del` 使用的**目标网段**（`address`/`ip`）必须是**目标机器尚未使用、且与 NPU 网段不冲突**的地址（示例中取 `10.30.40.0`、`10.30.50.0` 等），否则查询/断言会误判。注入前用 `-route -g`/`-ip_route -g` 确认目标网段不存在。
- `_del` 类用例的**前提**是目标项已存在：先注入对应 `_add` 类用例（或按 ④ 确认已在表内）再注入删除，否则报 `configuration does not exist` / `Route not exist`。
- 这些是**机器相关**参数：在另一台机器上请换成未占用的网段，如 `172.16.x.0`、`192.168.x.0`。

**⑥ MTU 取值原则**

- `mtu_mismatch` 的 `size` 必须**不同于当前真实 MTU**（示例中芯片 2 为 1500，故测试用 1280），否则 `-mtu -s` 是 no-op，query 会报「动作未生效」。
- 若前面用例把 MTU 改成了非 1500 的值且未恢复，需先 `hccn_tool -i 2 -mtu -s size 1500` 复位再注入（run_e2e 的 atexit 清理会自动恢复）。

**⑦ 状态残留与多用例串接**

NPU 注入会**真实改动芯片配置**。`_del` 类用例的 clean 依赖临时状态文件（sidecar）中保存的原值，若该文件丢失或进程被 kill，可能残留路由/IP rule。执行完一批用例后建议用 ④ 的命令核对并清理；run_e2e.py 在进程退出时会对 chip 2/5 做路由、arp、ip_rule、mtu 的自动清理。

---

### 5.1 rNPU_link_down — RoCE 链路 down

**UID**: `rNPU_link_down`

**描述**: 使指定芯片 RoCE 链路 down，阻断该芯片全部 RoCE 流量。

**实现原理**: inject 执行 `hccn_tool -i <chip> -link -s down`；clean 执行 `hccn_tool -i <chip> -cfg recovery`；query 执行 `-link -g` 检查是否包含 `down`。

**使用示例**:
```bash
dcat inject rNPU_link_down --chip=0
dcat query rNPU_link_down --chip=0
dcat clean rNPU_link_down --chip=0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |

**危险等级**: 高 — 直接切断该芯片所有 RoCE 流量，训练/推理任务全部中断。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。clean 依赖 `-cfg recovery`，若配置文件缺失可能无法恢复。

---

### 5.2 rNPU_ip_change — RoCE IP 变更

**UID**: `rNPU_ip_change`

**描述**: 修改指定芯片 RoCE 端口 IP 地址与掩码，导致连接中断。

**实现原理**: inject 先 `-ip -g` 取原值存入临时状态文件（sidecar，位于 `/tmp/dcat-<uid>-<chip>.bak`），再 `-ip -s address <addr> netmask <mask>` 覆盖；clean 从 sidecar 还原（缺省 `0.0.0.0/255.255.255.0`）；query 比对当前 IP 与原值。

**使用示例**:
```bash
dcat inject rNPU_ip_change --chip=0 --address=192.168.1.100 --netmask=255.255.255.0
dcat query rNPU_ip_change --chip=0
dcat clean rNPU_ip_change --chip=0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| address | 必填 | IPv4 | 新 IP 地址 |
| netmask | 必填 | IPv4 | 新子网掩码 |

**危险等级**: 高 — IP 变更后该芯片所有 RoCE 连接立即失效。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。临时状态文件存于 /tmp，重启或清理 /tmp 后无法 clean。

---

### 5.3 rNPU_gw_change — RoCE 网关变更

**UID**: `rNPU_gw_change`

**描述**: 修改指定芯片 RoCE 网关地址，导致跨网段路由失效。

**实现原理**: inject 先 `-gateway -g` 取原值存 sidecar（无原网关时存 `none`），再 `-gateway -s gateway <gw>` 修改；clean 从 sidecar 还原（原为 `none` 时跳过恢复）；query 比对当前网关与原值。

**使用示例**:
```bash
# ① 先查询当前 NPU IP 与网段（见「④ 前置参数查询」②）
hccn_tool -i 2 -ip -g          # 示例输出 ipaddr:10.30.12.9 netmask:255.255.255.0
# ② 查询当前网关，确认同网段基线值
hccn_tool -i 2 -gateway -g     # 示例输出 10.30.12.254

# ③ 注入一个新网关（必须与 NPU IP 同网段，且≠当前网关，否则 no-op）
dcat inject rNPU_gw_change --chip=2 --gateway=10.30.12.1   # 示例：基线 .254 → 改 .1
dcat query rNPU_gw_change --chip=2
dcat clean rNPU_gw_change --chip=2
```

> **参数说明**：`chip` 替换为目标芯片；`gateway` 必须是 **NPU IP 同网段**的一个地址（先在网段里换一个不同的主句号，如 .1/.2，不要与真实网关/对端冲突）。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| gateway | 必填 | IPv4 | 新网关地址，**必须与 NPU 当前 IP 同网段**，否则 `hccn_tool` 报 "segment doesn't match" |

**危险等级**: 高 — 网关错误后所有跨网段 RoCE 流量无法转发。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。**网关网段匹配**：注入前用 `hccn_tool -i <chip> -ip -g` 查询当前 NPU IP，网关必须在该 IP 的网段内。若 NPU 未设网关，inject 时原值保存为 `none`，clean 时跳过恢复（不设回任何网关）。**no-op 风险**：注入的 `gateway` 若与当前网关相同，`hccn_tool -gateway -s` 不会触发变更，query 会因状态未变而误判——务必先 `-gateway -g` 确认当前值，再注入一个不同的同网段地址。

---

### 5.4 rNPU_netdetect_change — Netdetect IP 变更

**UID**: `rNPU_netdetect_change`

**描述**: 修改指定芯片 netdetect 探测目标 IP，影响网络连通性检测。

**实现原理**: inject 先 `-netdetect -g` 取原值存 sidecar，再 `-netdetect -s address <addr>` 修改；clean 从 sidecar 还原；query 比对当前地址与原值。

**使用示例**:
```bash
dcat inject rNPU_netdetect_change --chip=0 --address=10.0.0.99
dcat query rNPU_netdetect_change --chip=0
dcat clean rNPU_netdetect_change --chip=0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| address | 必填 | IPv4 | 新 netdetect 探测地址 |

**危险等级**: 中 — netdetect 失效会导致健康检测误报，影响上层调度。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。

---

### 5.5 rNPU_arp_poison — ARP 毒化

**UID**: `rNPU_arp_poison`

**描述**: 向指定芯片注入错误 ARP 表项（伪造 MAC），使流量被误导。

**实现原理**: inject 执行 `-arp -a dev <dev> ip <ip> mac <mac>` 添加伪造 ARP；clean 执行 `-arp -d dev <dev> ip <ip>` 删除；query 检查 ARP 表中是否同时存在指定 ip 与 mac。

**使用示例**:
```bash
# ① 查出目标芯片的 RoCE 网口名（见「④ 前置参数查询」①）：示例机芯片 2 为 eth2
hccn_tool -i 2 -status -g   # 输出 "Settings for eth2" → dev 为 eth2

# ② 向该网口注入一条伪造 ARP（ip 选用 NPU 网段内一个未被占用的地址）
dcat inject rNPU_arp_poison --chip=2 --dev=eth2 --ip=10.30.12.200 --mac=00:11:22:33:44:55
dcat query rNPU_arp_poison --chip=2 --dev=eth2 --ip=10.30.12.200 --mac=00:11:22:33:44:55
dcat clean rNPU_arp_poison --chip=2 --dev=eth2 --ip=10.30.12.200 --mac=00:11:22:33:44:55
```

> **参数说明**：`dev` 必须用 `hccn_tool -i <chip> -status -g` 查出的真实网口名（示例为 eth2，勿照抄，不同机器可能为 eth0）；`ip` 用 NPU 同网段内未占用的地址。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| dev | 必填 | 字符串 | 网卡设备名（**NPU 内部名**，用 `hccn_tool -i <chip> -status -g` 确认目标芯片真实值；示例机 chip 2 为 eth2，勿照抄） |
| ip | 必填 | IPv4 | 被 poisoning 的目标 IP |
| mac | 必填 | MAC | 伪造的错误 MAC 地址 |

**危险等级**: 高 — 流量被静默导向错误 MAC，可能导致数据泄漏或连接中断。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。clean 仅按 ip+dev 删除。**`dev` 是 NPU 内部网口名**（示例机 chip 2 为 eth2，机器相关），不是 Linux 系统接口名（enp125s0f1 之类），配错会注入失败。

---

### 5.6 rNPU_arp_del — ARP 条目删除

**UID**: `rNPU_arp_del`

**描述**: 删除指定芯片 ARP 表项，导致对应 IP 流量停滞。

**实现原理**: inject 先 `-arp -g` 查询原 MAC 并存 sidecar，再 `-arp -d` 删除；clean 从 sidecar 取原 MAC 执行 `-arp -a` 重新添加（缺省 `00:00:00:00:00:00`）；query 检查指定 ip 是否已不存在。

**使用示例**:
```bash
# 前提：该 ARP 条目必须已存在！（dev 用目标芯片实际网口名，示例机 chip 2 为 eth2）
hccn_tool -i 2 -arp -g | grep 10.30.12.200   # 确认 ARP 条目存在

# 若不存在，先用 arp_poison 创建（ip 必须是 NPU 网段内未占用地址）
dcat inject rNPU_arp_poison --chip=2 --dev=eth2 --ip=10.30.12.200 --mac=de:ad:be:ef:00:01

# 删除 ARP 条目（inject 会自动保存原 MAC 到 sidecar）
dcat inject rNPU_arp_del --chip=2 --dev=eth2 --ip=10.30.12.200
dcat query rNPU_arp_del --chip=2 --dev=eth2 --ip=10.30.12.200
# clean 从 sidecar 恢复原 MAC
dcat clean rNPU_arp_del --chip=2 --dev=eth2 --ip=10.30.12.200
```

> **参数说明**：`dev` 用 `hccn_tool -i <chip> -status -g` 查出的真实网口名（示例机为 eth2）；`ip` 用 NPU 同网段未占用地址。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| dev | 必填 | 字符串 | NPU 内部网卡设备名（用 `hccn_tool -i <chip> -status -g` 确认目标芯片真实值，示例机 chip 2 为 eth2） |
| ip | 必填 | IPv4 | 要删除 ARP 表项的 IP |

**危险等级**: 中 — 删除后流量短暂停滞，通常可通过 ARP 重新学习自愈。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。**前提条件**：目标 ARP 条目必须已存在，否则 `hccn_tool -arp -d` 报 "The configuration does not exist"。建议先用 `rNPU_arp_poison` 创建 ARP 条目，再用 `rNPU_arp_del` 删除。clean 从 sidecar 读取原 MAC 恢复；若 sidecar 丢失则恢复为全零 MAC。**`dev` 必须与创建时的网口名一致**，否则 sidecar 匹配不到、clean 恢复失败。

---

### 5.7 rNPU_route_add — 添加 RoCE 路由

**UID**: `rNPU_route_add`

**描述**: 向指定芯片添加一条路由，可能误导流量走向错误网关。

**实现原理**: inject 执行 `-route -a address <addr> netmask <mask> gateway <gw>`；clean 执行 `-route -d address <addr> netmask <mask>` 删除；query 检查路由表是否包含该地址。

**使用示例**:
```bash
# ① 查询 NPU 当前 IP/网段与网关（见「④ 前置参数查询」②③）
hccn_tool -i 2 -ip -g         # 示例输出 ipaddr:10.30.12.9 netmask:255.255.255.0
hccn_tool -i 2 -gateway -g    # 示例输出 10.30.12.254

# ② gateway 必须与 NPU IP 同网段；address 选一个本机未使用的目标网段
dcat inject rNPU_route_add --chip=2 --address=10.30.40.0 --netmask=255.255.255.0 --gateway=10.30.12.254
dcat query rNPU_route_add --chip=2
dcat clean rNPU_route_add --chip=2 --address=10.30.40.0 --netmask=255.255.255.0
```

> **参数说明**：`gateway` 用 `-gateway -g` 查出的真实网关；`address` 选一个未占用的目标网段（如 `172.16.x.0`），且不能与 NPU 网段冲突。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| address | 必填 | IPv4 | 目标网段地址（如 `10.30.40.0`，须未使用、不与 NPU 网段冲突） |
| netmask | 必填 | IPv4 | 子网掩码（如 `255.255.255.0`） |
| gateway | 必填 | IPv4 | 下一跳网关，**必须与 NPU 当前 IP 同网段** |

**危险等级**: 中 — 错误路由可能将流量导向不可达网关。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。**网关网段匹配**：gateway 必须与 `hccn_tool -i <chip> -ip -g` 输出的 IP 同网段，否则 `hccn_tool` 报 "segment doesn't match"。**link 状态**：若 RoCE link 为 DOWN，需先 `hccn_tool -i <chip> -link -s up` 设置管理 UP（物理 link 可仍为 DOWN），否则 `route -a` 报 "Command execute failed"。**目标网段冲突**：`address` 若与已存在路由冲突，query 会因状态混乱断言失败，注入前用 `-route -g` 确认该网段不存在。

---

### 5.8 rNPU_route_del — 删除 RoCE 路由

**UID**: `rNPU_route_del`

**描述**: 删除指定芯片路由，导致对应网段不可达。

**实现原理**: inject 先 `-route -g` 查询原路由的 gateway 并存 sidecar，再 `-route -d` 删除；clean 从 sidecar 取原 gateway 执行 `-route -a` 重新添加（缺省 `0.0.0.0`）；query 检查该地址是否已不存在。

**使用示例**:
```bash
# 前提：该路由必须已存在！先用 route_add 创建，或确认路由表已有
hccn_tool -i 2 -route -g | grep 10.30.41.0   # 确认目标网段路由存在

# 若不存在，先注入 route_add（gateway 必须与 NPU IP 同网段）
dcat inject rNPU_route_add --chip=2 --address=10.30.41.0 --netmask=255.255.255.0 --gateway=10.30.12.254

# 删除路由（inject 会自动保存原 gateway 到 sidecar）
dcat inject rNPU_route_del --chip=2 --address=10.30.41.0 --netmask=255.255.255.0
dcat query rNPU_route_del --chip=2
# clean 会从 sidecar 读取原 gateway 自动恢复
dcat clean rNPU_route_del --chip=2 --address=10.30.41.0 --netmask=255.255.255.0
```

> **参数说明**：`address` 选一个未占用的目标网段，并先用 `route_add` 创建对应路由。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| address | 必填 | IPv4 | 要删除路由的目标网段（须未使用、不与 NPU 网段冲突） |
| netmask | 必填 | IPv4 | 子网掩码 |

**危险等级**: 高 — 删除关键路由后对应网段立即不可达。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。**前提条件**：目标路由必须已存在，否则 `hccn_tool -route -d` 报 "Route not exist in routing table, can not delete!"。建议先用 `rNPU_route_add` 创建路由，再用 `rNPU_route_del` 删除。clean 会从 sidecar 读取原 gateway 自动恢复路由；若 sidecar 丢失则恢复为 `0.0.0.0`。**注意**：`route_add`/`route_del` 的 `address` 与 `route` 模块（5.7）不能使用同一网段以免相互干扰——示例中 route_add 用 `10.30.40.0`，route_del 用 `10.30.41.0`。

---

### 5.9 rNPU_iprule_add — 添加 ip rule

**UID**: `rNPU_iprule_add`

**描述**: 向指定芯片添加策略路由规则，可能改变流量选路。

**实现原理**: inject 执行 `-ip_rule -a dir <dir> ip <ip> table <table>`；clean 执行 `-ip_rule -d dir <dir> ip <ip>` 删除；query 检查是否同时存在指定 ip 与 table。

**使用示例**:
```bash
# ip 选用 NPU 网段内一个未被占用、且与已有 ip_rule 不冲突的地址
dcat inject rNPU_iprule_add --chip=2 --dir=from --ip=10.30.12.210 --table=150
dcat query rNPU_iprule_add --chip=2
dcat clean rNPU_iprule_add --chip=2 --dir=from --ip=10.30.12.210 --table=150
```

> **参数说明**：`ip` 用 NPU 同网段内未占用地址，并避免与 iprule_del（5.10）用同一 ip；`table` 用一个未被占用的表号。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| dir | 必填 | from/to/in/out | 策略匹配方向 |
| ip | 必填 | IPv4 | 策略匹配的源/目的 IP（示例中用 10.30.12.210，须未占用） |
| table | 必填 | 整数 | 路由表编号（本机用 150，须未被占用） |

**危险等级**: 中 — 受匹配的流量将改走指定路由表，可能改变选路结果。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。注入前用 `hccn_tool -i <chip> -ip_rule -g` 查看已有 ip rule，避免 `ip`/`table` 与现有规则冲突，否则 query 会误判。iprule_add 与 iprule_del（5.10）在示例中分别用 10.30.12.210 与 10.30.12.211，避免相互干扰。

---

### 5.10 rNPU_iprule_del — 删除 ip rule

**UID**: `rNPU_iprule_del`

**描述**: 删除指定芯片策略路由规则，可能破坏策略选路。

**实现原理**: inject 先 `-ip_rule -g` 取原 table 存 sidecar，再 `-ip_rule -d` 删除；clean 从 sidecar 取原 table 执行 `-ip_rule -a` 重新添加（缺省 table 0）；query 检查该 ip 是否已不存在。

**使用示例**:
```bash
# 前提：该 ip rule 必须已存在！先注入 iprule_add 创建
dcat inject rNPU_iprule_add --chip=2 --dir=from --ip=10.30.12.211 --table=150

# 删除 ip rule（inject 自动保存原 table 到 sidecar）
dcat inject rNPU_iprule_del --chip=2 --dir=from --ip=10.30.12.211
dcat query rNPU_iprule_del --chip=2
dcat clean rNPU_iprule_del --chip=2 --dir=from --ip=10.30.12.211
```

> **参数说明**：`ip` 用 NPU 同网段未占用地址，并先注入 `iprule_add` 创建该规则。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| dir | 必填 | from/to/in/out | 策略匹配方向 |
| ip | 必填 | IPv4 | 策略匹配的源/目的 IP |

**危险等级**: 中 — 删除策略规则后受影响的流量可能回落到主路由表。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。**前提条件**：目标 ip rule 必须已存在，否则 `hccn_tool -ip_rule -d` 报 "configuration does not exist"。建议先用 `rNPU_iprule_add` 创建规则（确保 `dir`/`ip` 一致）再删除。clean 从临时状态文件读取原 table 恢复；示例中 iprule_del 与 iprule_add 分别用 10.30.12.211/210，避免相互干扰。

---

### 5.11 rNPU_iproute_add — 添加 ip route

**UID**: `rNPU_iproute_add`

**描述**: 向指定芯片策略路由表添加一条路由，可能误导流量。

**实现原理**: inject 执行 `-ip_route -a ip <ip> ip_mask <mask> via <via> dev <dev> table <table>`；clean 执行 `-ip_route -d ip <ip> ip_mask <mask> table <table>` 删除；query 检查该 table 是否包含指定 ip。

**使用示例**:
```bash
# ① 查询 NPU 当前 IP/网段与网关（见「④ 前置参数查询」②③）
hccn_tool -i 2 -ip -g         # 示例输出 ipaddr:10.30.12.9 netmask:255.255.255.0
hccn_tool -i 2 -gateway -g    # 示例输出 10.30.12.254

# ② via 必须与 NPU IP 同网段；ip_mask 是 CIDR 位数；dev 是 NPU 内部名（示例机为 eth2）
dcat inject rNPU_iproute_add --chip=2 --ip=10.30.50.0 --ip_mask=24 --via=10.30.12.254 --dev=eth2 --table=100
dcat query rNPU_iproute_add --chip=2
dcat clean rNPU_iproute_add --chip=2 --ip=10.30.50.0 --ip_mask=24 --table=100
```

> **参数说明**：`via` 用真实网关；`dev` 用 `hccn_tool -i <chip> -status -g` 查出的真实网口名（示例机为 eth2）；`ip` 选一个未占用的目标网段；`table` 用未被占用的表号。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| ip | 必填 | IPv4 | 目标网段地址（如 `10.30.50.0`，须未使用、不与 NPU 网段冲突） |
| ip_mask | 必填 | 整数 0-32 | **CIDR 位数**（如 `24` 表示 /24），不是点分掩码 |
| via | 必填 | IPv4 | 下一跳地址，**必须与 NPU 当前 IP 同网段** |
| dev | 必填 | 字符串 | NPU 内部网卡名（用 `hccn_tool -i <chip> -status -g` 确认目标芯片真实值，示例机为 eth2，非 Linux 接口名） |
| table | 必填 | 整数 0-255 | 路由表编号 |

**危险等级**: 中 — 添加路由可能改变选路结果。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。**ip_mask 是 CIDR 位数**（0-32），不是点分十进制掩码（如 `255.255.255.0`→`24`）。**via 网段匹配**：via 必须与 NPU IP 同网段。**dev 是 NPU 内部名**：机器相关，示例机为 `eth2`，可用 `hccn_tool -i <chip> -status -g` 确认目标芯片真实值，不是 Linux 系统接口名（如 `enp125s0f1`）。**目标网段冲突**：`ip` 与已存在路由冲突会使 query 断言失败。

---

### 5.12 rNPU_iproute_del — 删除 ip route

**UID**: `rNPU_iproute_del`

**描述**: 删除指定芯片路由表中指定路由，导致对应网段不可达。

**实现原理**: inject 先 `-ip_route -g table <table>` 取原 via/dev 存 sidecar，再 `-ip_route -d` 删除；clean 从 sidecar 取原 via/dev 执行 `-ip_route -a` 重新添加（缺省 `via=0.0.0.0 dev=eth0`）；query 检查该 ip 是否已不存在。

**使用示例**:
```bash
# 前提：该 ip_route 必须已存在！先用 iproute_add 创建（via 与 NPU IP 同网段，dev 用实际网口名，示例机为 eth2）
dcat inject rNPU_iproute_add --chip=2 --ip=10.30.51.0 --ip_mask=24 --via=10.30.12.254 --dev=eth2 --table=100

# 删除 ip_route
dcat inject rNPU_iproute_del --chip=2 --ip=10.30.51.0 --ip_mask=24 --table=100
dcat query rNPU_iproute_del --chip=2 --table=100
# clean 从 sidecar 恢复原路由
dcat clean rNPU_iproute_del --chip=2 --ip=10.30.51.0 --ip_mask=24 --table=100
```

> **参数说明**：`ip`/`table` 与 iproute_add（5.11）保持一致（示例分别用 10.30.51.0/10.30.50.0、table 100），`ip_mask` 为 CIDR 位数，`dev` 为真实网口名。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| ip | 必填 | IPv4 | 要删除路由的目标网段 |
| ip_mask | 必填 | 整数 0-32 | **CIDR 位数**（如 `24` 表示 /24） |
| table | 必填 | 整数 0-255 | 路由表编号 |

**危险等级**: 高 — 删除路由后对应网段立即不可达，clean 依赖 sidecar 中的原 via/dev。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。**前提条件**：目标 ip_route 必须已存在，否则 `hccn_tool -ip_route -d` 报 "configuration does not exist"。建议先用 `rNPU_iproute_add` 创建路由。**ip_mask 是 CIDR 位数**（0-32），不是点分掩码。clean 从临时状态文件读取原 via/dev/table 恢复；示例中 iproute_del 与 iproute_add 分别用 10.30.51.0/10.30.50.0 避免网段冲突。

---

### 5.13 rNPU_bw_limit — RoCE 带宽限速

**UID**: `rNPU_bw_limit`

**描述**: 对指定芯片 RoCE 流量进行带宽限速，降低吞吐。

**实现原理**: inject 执行 `-shaping -s bw_limit <bw>` 设置限速；clean 执行 `-shaping -s bw_limit 100000`（MAX_BW 常量）恢复为最大带宽；query 检查当前 bw_limit 是否小于 MAX_BW。

**使用示例**:
```bash
dcat inject rNPU_bw_limit --chip=0 --bw_limit=1000
dcat query rNPU_bw_limit --chip=0
dcat clean rNPU_bw_limit --chip=0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| bw_limit | 必填 | 整数 (Mbps) | 带宽限速值 |

**危险等级**: 中 — 限速不破坏链路，但显著降低 RoCE 吞吐。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。clean 恢复值为硬编码 MAX_BW=100000，若芯片原始限速非该值则无法精确还原。

---

### 5.14 rNPU_mtu_mismatch — RoCE MTU 变更

**UID**: `rNPU_mtu_mismatch`

**描述**: 修改指定芯片 RoCE MTU，造成 MTU 不匹配引发分片/丢包。

**实现原理**: inject 先 `-mtu -g` 取原值存 sidecar，再 `-mtu -s size <size>` 修改；clean 从 sidecar 还原（缺省 1500）；query 比对当前 MTU 与原值。

**使用示例**:
```bash
# ① 查询当前 MTU（见「④ 前置参数查询」④）：示例中芯片 2 为 1500
hccn_tool -i 2 -mtu -g   # 输出 1500

# ② size 必须 ≠ 当前 MTU；若之前用例已改成非 1500，先复位
dcat inject rNPU_mtu_mismatch --chip=2 --size=1280
dcat query rNPU_mtu_mismatch --chip=2
dcat clean rNPU_mtu_mismatch --chip=2
```

> **参数说明**：`size` 用一个与当前 MTU **不同**的字节数（示例中当前 1500 → 注入 1280）。若当前 MTU 已是 1280，则改成 1500 或 9000 才有效。

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| size | 必填 | 整数 (字节) | MTU 字节数，如 1500、9000 |

**危险等级**: 中 — MTU 与对端不匹配导致大包分片或被丢弃，小包不受影响，问题隐蔽。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。**no-op 风险**：注入的 `size` 若与当前 MTU 相同，`-mtu -s` 不会触发变更，query 会报「动作未生效」。注入前用 `-mtu -g` 确认当前值，选择一个不同的 size；若前面用例把 MTU 改为非 1500 且未恢复，需先 `hccn_tool -i <chip> -mtu -s size <原值>` 复位再注入。

---

### 5.15 rNPU_dscp_tc_change — DSCP→TC 映射变更

**UID**: `rNPU_dscp_tc_change`

**描述**: 修改指定芯片 DSCP 到 TC 映射，打乱 QoS 流量分类。

**实现原理**: inject 先 `-dscp_to_tc -g dscp <dscp>` 取原 tc 存 sidecar，再 `-dscp_to_tc -s dscp <dscp> tc <tc>` 修改；clean 从 sidecar 还原（缺省 0）；query 比对当前 tc 与原值。

**使用示例**:
```bash
dcat inject rNPU_dscp_tc_change --chip=0 --dscp=46 --tc=0
dcat query rNPU_dscp_tc_change --chip=0 --dscp=46
dcat clean rNPU_dscp_tc_change --chip=0 --dscp=46 --tc=0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| dscp | 必填 | 0-63 | DSCP 差分服务代码点 |
| tc | 必填 | 整数 | 流量类编号，通常 0-7 |

**危险等级**: 中 — 映射错误使高优先级流量被降级调度，QoS 失效，但链路本身仍通。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。仅影响指定 DSCP 值的映射。

---

### 5.16 rNPU_roce_port_change — RoCE UDP 端口变更

**UID**: `rNPU_roce_port_change`

**描述**: 修改指定芯片 RoCE UDP 端口，导致与对端 RoCEv2 通信中断。

**实现原理**: inject 先 `-udp -g` 取原 port 存 sidecar，再 `-udp -s port <port>` 修改；clean 从 sidecar 还原（缺省 4791，即 RoCEv2 标准端口）；query 比对当前 port 与原值。

**使用示例**:
```bash
dcat inject rNPU_roce_port_change --chip=0 --port=4792
dcat query rNPU_roce_port_change --chip=0
dcat clean rNPU_roce_port_change --chip=0
```

**参数可选范围**:
| 参数 | 是否必填 | 类型 | 说明 |
|---|---|---|---|
| chip | 必填 | 0-7 | NPU 芯片号 |
| port | 必填 | 1-65535 | RoCE UDP 端口号，默认 4791 |

**危险等级**: 高 — 端口非 4791 时对端按标准 RoCEv2 发包将无法匹配，该芯片所有 RoCEv2 流量中断。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。clean 缺省回退值为 4791（RoCEv2 标准端口）。
