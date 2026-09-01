# DemonCAT 用户手册

> DemonCAT（`dcat`）—— Linux 计算故障注入工具。
> 覆盖 CPU / 存储 / 网络 / 进程 / 内存 / 文件系统 / Docker / NPU / 系统九大模块，共 58 条故障。
> 完整规格见 [SPEC.md](SPEC.md)，架构见 [docs/DESIGN.md](docs/DESIGN.md)，故障速查见 [docs/DemonCAT_Error_List.md](docs/DemonCAT_Error_List.md)。
>
> **命令约定**：本手册所有示例均以 `dcat` 形式书写。编译后执行一次 `sudo make install` 即可在 `/usr/local/bin` 创建全局入口（符号链接指向 `build/dcat`，后续更新只需 `git pull`，无需重新安装）；若未执行此步，请将 `dcat` 替换为 `./build/dcat`。

---

## 故障能力清单

| 模块 | 条数 | 故障范围 |
| --- | :---: | --- |
| CPU | 5 | 核满载 / 核离线 / CPU 限额 / 降频 / RT 核饿死 |
| 存储 | 5 | 磁盘写压 / 分区填满 / inode 耗尽 / IO 延迟 / IO 错误 |
| 网络 | 13 | 延迟 / 丢包 / 乱序 / 网卡 down / 降速 / 端口占用 / 服务停止 / 链路闪断 / 带宽限制 / 抖动 / TCP 丢包 / 包损坏 / 连接耗尽 |
| 进程 | 5 | 进程退出 / 挂起 / 僵尸 / fork 炸弹 / FD 耗尽 |
| 内存 | 4 | 内存泄漏 / OOM / 内存碎片 / Swap 过载 |
| 文件系统 | 2 | 文件锁 / iowait 飙高 |
| Docker | 2 | 容器 kill / 容器内存过载 |
| NPU | 20 | RoCE 链路 / IP / 网关 / ARP / 路由 / 策略路由 / ip route / 带宽 / MTU / DSCP / RoCE 端口 / PCIe 降速 / AICore 负载 / AICpu 负载 / AIVector 负载 / HBM 负载 / 芯片复位 / 驱动解绑 / PCIe 拔卡 / Netdetect |
| 系统 | 2 | 内核 panic / 下电重启 |
| **合计** | **58** | |

---

## 目录

- [故障能力清单](#故障能力清单)
- [通用约定：重注入与 --force](#通用约定重注入与---force)
- [通用约定：clean --all 与 stateless clean](#通用约定clean---all-与-stateless-clean)
- [第一章 CPU 模块](#第一章-cpu-模块5-条)
    - [1.1 rCPU_overload](#11-rcpu_overload) — 核满载
    - [1.2 rCPU_core_offline](#12-rcpu_core_offline) — 核离线
    - [1.3 rCPU_quota](#13-rcpu_quota) — CPU 限额（cgroup）
    - [1.4 rCPU_freq](#14-rcpu_freq) — CPU 降频
    - [1.5 rCPU_core_hang](#15-rcpu_core_hang) — RT 核饿死
- [第二章 存储模块](#第二章-存储模块5-条)
    - [2.1 rDISK_write_overload](#21-rdisk_write_overload) — 磁盘写压
    - [2.2 rDISK_part_full](#22-rdisk_part_full) — 分区填满
    - [2.3 rDISK_inode_exhaust](#23-rdisk_inode_exhaust) — inode 耗尽
    - [2.4 rDISK_io_delay](#24-rdisk_io_delay) — IO 延迟
    - [2.5 rDISK_io_error](#25-rdisk_io_error) — IO 错误
- [第三章 网络模块](#第三章-网络模块13-条)
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
    - [3.12 rNET_corrupt](#312-rnet_corrupt) — 包损坏
    - [3.13 rNET_conn_exhaust](#313-rnet_conn_exhaust) — 连接耗尽
- [第四章 进程模块](#第四章-进程模块5-条)
    - [4.1 rPROC_exit](#41-rproc_exit) — 进程退出
    - [4.2 rPROC_hang](#42-rproc_hang) — 进程挂起
    - [4.3 rPROC_zstate](#43-rproc_zstate) — 僵尸进程
    - [4.4 rPROC_fork_bomb](#44-rproc_fork_bomb) — fork 炸弹
    - [4.5 rPROC_fd_exhaust](#45-rproc_fd_exhaust) — FD 耗尽
- [第五章 内存模块](#第五章-内存模块4-条)
    - [5.1 rMEM_leak](#51-rmem_leak) — 内存泄漏
    - [5.2 rMEM_oom](#52-rmem_oom) — OOM
    - [5.3 rMEM_fragment](#53-rmem_fragment) — 内存碎片
    - [5.4 rMEM_swap_overload](#54-rmem_swap_overload) — Swap 过载
- [第六章 文件系统模块](#第六章-文件系统模块2-条)
    - [6.1 rFS_file_lock](#61-rfs_file_lock) — 文件锁
    - [6.2 rFS_iowait_high](#62-rfs_iowait_high) — iowait 飙高
- [第七章 Docker 模块](#第七章-docker-模块2-条)
    - [7.1 rDOCKER_kill](#71-rdocker_kill) — 容器 kill
    - [7.2 rDOCKER_mem_overload](#72-rdocker_mem_overload) — 容器内存过载
- [第八章 NPU 模块](#第八章-npu-模块20-条)
    - [8.0 前置准备](#80-前置准备实机参数查询与调整)
    - [8.1 rNPU_link_down](#81-rnpu_link_down) — RoCE 链路 down
    - [8.2 rNPU_ip_change](#82-rnpu_ip_change) — RoCE IP 变更
    - [8.3 rNPU_gw_change](#83-rnpu_gw_change) — RoCE 网关变更
    - [8.4 rNPU_netdetect_change](#84-rnpu_netdetect_change) — Netdetect IP 变更
    - [8.5 rNPU_arp](#85-rnpu_arp) — ARP 操控
    - [8.6 rNPU_route](#86-rnpu_route) — RoCE 路由操控
    - [8.7 rNPU_iprule](#87-rnpu_iprule) — ip rule 操控
    - [8.8 rNPU_iproute](#88-rnpu_iproute) — ip route 操控
    - [8.9 rNPU_bw_limit](#89-rnpu_bw_limit) — RoCE 带宽限速
    - [8.10 rNPU_mtu_mismatch](#810-rnpu_mtu_mismatch) — RoCE MTU 变更
    - [8.11 rNPU_dscp_tc_change](#811-rnpu_dscp_tc_change) — DSCP→TC 映射变更
    - [8.12 rNPU_roce_port_change](#812-rnpu_roce_port_change) — RoCE UDP 端口变更
    - [8.13 rNPU_pcie_down](#813-rnpu_pcie_down) — NPU PCIe 降速
    - [8.14 rNPU_aic_load](#814-rnpu_aic_load) — AICore 负载
    - [8.15 rNPU_aicpu_load](#815-rnpu_aicpu_load) — AICpu 负载
    - [8.16 rNPU_aiv_load](#816-rnpu_aiv_load) — AIVector 负载
    - [8.17 rNPU_hbm_load](#817-rnpu_hbm_load) — HBM 负载
    - [8.18 rNPU_chip_reset](#818-rnpu_chip_reset) — 芯片复位
    - [8.19 rNPU_driver_unbind](#819-rnpu_driver_unbind) — 驱动解绑
    - [8.20 rNPU_pcie_remove](#820-rnpu_pcie_remove) — PCIe 拔卡
- [第九章 系统模块](#第九章-系统模块2-条)
    - [9.1 rSYS_panic](#91-rsys_panic) — 内核 panic
    - [9.2 rSYS_poweroff](#92-rsys_poweroff) — 下电重启
- [第十章 Web 控制台（dcat serve）](#第十章-web-控制台dcat-serve)

---

## 通用约定：重注入与 --force

dcat 对**同一资源的重复注入默认拒绝**（退出码 5），需显式 `--force` 才原子替换。这避免意外的故障叠加/资源冲突（如两次 CPU 满载抢核、两个 tc qdisc 打架）。

- **资源键**：各故障的 `clean_required` 参数（见每章参数表）。`cores` 走核集交集，其余走精确等值。
- 同资源（同 iface / 重叠核 / 同 pid）重注入 → **拒绝**；加 `--force` → 先清旧再注新（原子替换）。
- 不同资源（不同 iface / 不重叠核段）→ 并发注入 OK，互不影响。
- inject-only 故障（如 `rPROC_exit`、`rSYS_panic`）不写 state，可重复 inject。
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

## 第一章 CPU 模块（5 条）

### 1.1 rCPU_overload — 核满载（perl 纯用户态）

**UID**: `rCPU_overload`

**描述**: 通过 `taskset` 绑定到指定 CPU 核运行纯用户态死循环（`perl -e '1 while 1'`），使指定核 100% 用户态满载。

**实现原理**:

- **inject**: 从 `DCAT_PARAM_CORES` 取必填的 cores 规格（支持 `0,2,4` / `0-3` / `0-3,7` 等混合格式，由 `parse_cores` 展开为单核号列表）。对每个核，用 `taskset -c <n>` 绑定启动 `perl -e '1 while 1'`（纯用户态死循环，无系统调用开销），后台运行并重定向到 `/dev/null`；若系统无 perl，自动回退为 `yes`（会引入约 60% 系统调用开销）。将所有子进程 pid 写入 pidfile `/tmp/dcat-rCPU_overload-${spec}.pid`（spec 为原始参数串），输出注入结果。
- **clean**: 读取 pidfile，逐个 `kill` 进程并删除 pidfile；若 pidfile 不存在则报错并 `exit 1`。
- **query**: 通过 `/proc/stat` 双采样 delta 计算指定核的用户态占比（`%us`），并列出各 burn 进程的 `pid/%cpu/psr/cmd` 明细。任一所查核占用率 > 0（或进程存活）时返回成功（exit 0），未找到任何活跃 burn 进程返回失败（exit 1）。**无 `--cores` 参数时查询全部已注入的核**（从 pidfile glob 探测）；带 `--cores=0,1` 则只查指定核。

**使用示例**:

```bash
dcat inject rCPU_overload --cores=0,1
dcat query  rCPU_overload                 # 无参 = 查全部在线核
dcat query  rCPU_overload --cores=0,1     # 只查 0,1 号核
dcat clean  rCPU_overload --cores=0,1
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| cores | inject/clean 必填；query 可选 | 核号列表 | 支持 `"0,2,4"` 或 `"0-3"` 或 `"0-3,7"` 格式；query 缺省时查全部在线核 |
| load_pct | 可选 | 整数 | 每核负载百分比（1–100），默认 100；通过 `taskset -c` 绑核后控制 perl 进程的 CPU 占用率 |

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
| --- | --- | --- | --- |
| cores | 必填 | 核号列表 | 支持 `"0,2,4"` 或 `"0-3"` 格式；cpu0 通常不可下线，自动跳过 |

**危险等级**: 高 — 直接将 CPU 核下线，减少系统可用算力；下线多核可能触发调度器重平衡与 NUMA 重排。

**补充说明**: 需要 root 权限写 sysfs；依赖内核 `CONFIG_HOTPLUG_CPU` 支持。部分虚拟化/容器环境不支持核下线。clean 仅恢复实际下线成功的核。核离线在部分内核版本上可能引起系统宕机，请谨慎使用；另外 **cpu0（0 号核心）在很多内核/平台上无法离线**，脚本会自动跳过并告警，这是正常现象，不代表故障注入失败。

---

### 1.3 rCPU_quota — CPU 核心限幅（cgroup）

**UID**: `rCPU_quota`

**描述**: 通过 cgroup 将指定 CPU 核心的最大使用率限制在 `quota_pct`%（1–99%）。创建 cgroup 设定 `cpuset.cpus`（限定核）+ `cpu.max`（限额度），并将该核上现有进程移入 cgroup。`top` 查看该核占用即被限制为目标值。

**实现原理**:

- **inject**: 创建 cgroup，设 `cpuset.cpus=<cores>` 限定核 + `cpu.max=<quota_pct>%` 限额度。扫描 `/proc/[0-9]*/stat` field 39（last processor）找出目标核上的进程，移入 cgroup。PID 列表存入 sidecar。
- **clean**: 将所有进程移回根 cgroup，恢复原值，删除 cgroup。
- **query**: 显示受限核号、限额百分比和实际使用率（采样 /proc/stat 1 秒）。

**使用示例**:

```bash
# 限制核心 1 到 30% CPU 使用率
dcat inject rCPU_quota --cores=1 --quota_pct=30
dcat query  rCPU_quota
dcat clean  rCPU_quota

# 限制核心 0,2,4 到 50% CPU
dcat inject rCPU_quota --cores=0,2,4 --quota_pct=50
dcat clean  rCPU_quota
```

> **典型场景**：先注入 `rCPU_overload --cores=1` 满载核心 1（100%），再注入 `rCPU_quota --cores=1 --quota_pct=30`，核心 1 占用从 100% 降至 30%（burn 进程被 cgroup 限流）。`top` / `mpstat` 可验证。

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| cores | inject 必填 | 核号列表 | 支持 `"0,2,4"` 或 `"0-3"` 格式 |
| quota_pct | inject 必填 | 整数 1–99 | CPU 核心最大使用率百分比 |

**危险等级**: 中 — 限制目标核上所有进程的 CPU 调度配额，可能导致进程响应变慢。clean 后恢复。

> **依赖与前提**:
>
> - **root 权限**：需写 `/sys/fs/cgroup/` 下的文件
> - **cgroup v1**：需 `/sys/fs/cgroup/cpu/`（cpu 控制器）和 `/sys/fs/cgroup/cpuset/`（cpuset 控制器）层级存在。检查：`ls /sys/fs/cgroup/cpu/ /sys/fs/cgroup/cpuset/`。若缺失，需内核启用 `CONFIG_CGROUP_SCHED` + `CONFIG_CPUSETS` + `CONFIG_CFS_BANDWIDTH`，并挂载对应层级
> - **cgroup v2**：需 `cgroup.controllers` 包含 `cpu` 和 `cpuset`。检查：`cat /sys/fs/cgroup/cgroup.controllers`。若缺失，需内核启用对应控制器
> - **Docker**：容器需 `--privileged` 或挂载 `/sys/fs/cgroup`（本工具的 docker-compose 已配置 `privileged: true`）
> - **注意**：inject 后新启动的进程不会自动进入 cgroup。正确顺序是先注入负载（overload 等），再注入 quota

---

### 1.4 rCPU_freq — CPU 降频

**UID**: `rCPU_freq`

**描述**: 通过 sysfs 设置 `scaling_max_freq` 限制指定 CPU 核的最高频率（降频），模拟 CPU 性能降级。

**实现原理**:

- **inject**: 对每个核，读取原 `scaling_max_freq` 和 `scaling_min_freq` 存入 sidecar。若目标频率低于 `scaling_min_freq`（策略下限），先降低 `scaling_min_freq` 再设置 `scaling_max_freq`。写入后回读验证，不匹配则回滚所有已改核并退出。
- **clean**: 先恢复 `scaling_max_freq`（抬上限），再恢复 `scaling_min_freq`（抬下限），避免 min > max 被内核拒绝。
- **query**: 打印每核当前 `scaling_max_freq`。

**使用示例**:

```bash
dcat inject rCPU_freq --cores=0,1 --freq_mhz=800
dcat query  rCPU_freq --cores=0,1
dcat clean  rCPU_freq
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| cores | inject 必填；query 可选 | 核号列表 | 支持 `"0,2,4"` 或 `"0-3"` 格式 |
| freq_mhz | inject 必填 | 正整数 | 目标最高频率（**MHz**，非 kHz/GHz）。有效范围 = `cpuinfo_min_freq` ~ `cpuinfo_max_freq`，可用 `cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq` 查看（单位 kHz，除以 1000 即 MHz） |

**危险等级**: 中 — 降低 CPU 频率影响该核上所有任务的计算性能。

> **单位说明**: sysfs `scaling_max_freq` 以 kHz 为单位，`freq_mhz` 参数以 MHz 为单位（脚本内部 `× 1000` 转 kHz）。例如 `--freq_mhz=800` = 800 MHz = 0.8 GHz = 800000 kHz。

**补充说明**: 需要 root 权限写 sysfs；依赖内核 `CONFIG_CPU_FREQ` 及 cpufreq 驱动。部分虚拟化/容器环境不暴露 cpufreq sysfs。频率值必须小于当前 `scaling_max_freq` 才有实际效果。clean 从 sidecar 恢复，无 sidecar 则跳过。

---

### 1.5 rCPU_core_hang — RT 核饿死

**UID**: `rCPU_core_hang`

**描述**: 通过 `chrt -f 99`（SCHED_FIFO 实时调度）+ `taskset` 绑核，在每个目标核上运行 RT 优先级死循环，饿死同核上的普通进程。

**实现原理**:

- **inject**: 对每个核，用 `chrt -f 99 taskset -c <n> sh -c 'while :; do :; done'` 启动 RT 99 优先级的忙循环进程。pid 写入 pidfile。
- **clean**: 读取 pidfile，`kill` 所有 RT 进程。
- **query**: 统计存活的 RT 循环进程数。

**使用示例**:

```bash
dcat inject rCPU_core_hang --cores=0,1
dcat query  rCPU_core_hang
dcat clean  rCPU_core_hang
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| cores | inject 必填 | 核号列表 | 支持 `"0,2,4"` 或 `"0-3"` 格式 |

**危险等级**: 高 — RT 99 优先级会完全霸占目标核，同核普通进程可能长时间无法调度，系统可能卡顿。

**补充说明**: 需要 root 权限和 `chrt`（util-linux）。与 `rCPU_overload` 的区别：overload 用普通调度（SCHED_OTHER）的纯用户态满载；core_hang 用 RT 调度（SCHED_FIFO 99）抢占同核所有普通进程。如果内核 `RT throttling`（`/proc/sys/kernel/sched_rt_runtime_us`）开启，RT 进程会被自动节流。

---

## 第二章 存储模块（5 条）

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
| --- | --- | --- | --- |
| device | 必填 | 设备/目录路径 | 写入目标；目录则在其下写 `dcat.stress.*`，非目录则写 `/tmp/dcat.write.*` |
| workers | 可选 | 整数 | 并发 dd 写进程数，默认 `4` |
| size_mb | 可选 | 整数 | 单次 dd 写入块大小（MB），默认 `200` |

**危险等级**: 中 — 持续写盘占用 IO 带宽并消耗磁盘空间，拖慢同盘其他 IO 任务；长期运行可能写满磁盘。

**补充说明**: clean 必须传入与 inject 相同的 device（pidfile 按路径命名）。建议配合 `size_mb` 控制单轮写入量以避免过快写满磁盘。

---

### 2.2 rDISK_part_full — 分区填满

**UID**: `rDISK_part_full`

**描述**: 在目标路径创建大填充文件，快速消耗磁盘空间直至填满（或达到指定大小）。

**实现原理**:

- **inject**: 在 `path` 下创建填充文件 `dcat.fill.<pid>`。若指定 `size`，用 `dd` 或 `fallocate` 创建指定大小文件（支持 `100M`/`2G` 等单位）；若省略 `size`，持续写入直到 ENOSPC。路径存入 sidecar。
- **clean**: 从 sidecar 读取路径，删除填充文件。
- **query**: 检查填充文件是否存在，打印大小与 `df`。

**使用示例**:

```bash
# 填 2GB
dcat inject rDISK_part_full --path=/data --size=2G
dcat query  rDISK_part_full --path=/data
dcat clean  rDISK_part_full

# 持续填充直至磁盘满
dcat inject rDISK_part_full --path=/data
dcat clean  rDISK_part_full
```

> **Ctrl+C 中断后清理**：省略 `--size` 时 `dd` 持续填充至 ENOSPC，耗时较长。若 Ctrl+C 中断 inject，`dcat clean rDISK_part_full --path=<path>` 会报 `no active injection`（inject 未完成、未写 state 记录）；此时用 **`dcat clean --all` 兜底**清残留填充文件（stateless fan-out，不依赖 state 记录，脚本自行扫描 `/tmp` sidecar 清理）。

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| path | inject 必填；query 可选 | 目录路径 | 目标挂载点/目录 |
| size | 可选 | 大小字符串 | 填充大小（`100`=`100MB`、`100M`、`2G`）；省略则持续填充至 ENOSPC |

**危险等级**: 高 — 填满磁盘会导致同分区所有写入操作失败（ENOSPC），可能影响数据库、日志等服务。

**补充说明**: 需要 root 或对目标路径的写权限。优先使用 `fallocate`（快速），回退 `dd`。clean 删除 sidecar 中记录的填充文件路径。

---

### 2.3 rDISK_inode_exhaust — inode 耗尽

**UID**: `rDISK_inode_exhaust`

**描述**: 在目标路径下创建大量空文件，耗尽文件系统 inode（即使磁盘空间未满也无法创建新文件）。

**实现原理**:

- **inject**: 在 `path/dcat.inodes.<pid>/` 下循环创建空文件 `f0, f1, ...`，直到达到 `count` 或创建失败（inode 耗尽）。目录路径存入 sidecar。
- **clean**: `rm -rf` sidecar 中记录的目录。
- **query**: 打印创建的文件数与 `df -i`。

**使用示例**:

```bash
dcat inject rDISK_inode_exhaust --path=/data --count=100000
dcat query  rDISK_inode_exhaust
dcat clean  rDISK_inode_exhaust
```

> **Ctrl+C 中断后清理**：大 `count`（或省略 `count` 耗尽到 ENOSPC）耗时较长。若 Ctrl+C 中断 inject，带参 `clean` 会报 `no active injection`（未写 state 记录）；此时用 **`dcat clean --all` 兜底**清残留的 `dcat.inodes.*` 目录。

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| path | inject 必填 | 目录路径 | 目标挂载点/目录 |
| count | 可选 | 整数 | 最多创建的文件数，默认 `100000` |

**危险等级**: 高 — inode 耗尽后无法创建任何新文件，即使磁盘空间充足。影响同分区内所有创建文件/目录的操作。

**补充说明**: 需要对目标路径的写权限。inode 耗尽比空间填满更隐蔽（`df` 显示空间充足但 `df -i` 显示 inode 已满）。clean 删除整个 `dcat.inodes.*` 目录。

---

### 2.4 rDISK_io_delay — IO 延迟（dm-delay）

**UID**: `rDISK_io_delay`

**描述**: 通过 device-mapper `delay` 目标在块设备上叠加**读与写延迟**，所有 IO 操作被延迟指定毫秒数。

**实现原理**:

- **inject**: 在块设备 `device` 上创建 dm-delay 设备 `dcat-delay-<devname>`，使用 `dmsetup create` 设置 `delay <delay_ms> <delay_ms>`（前为读延迟偏移、后为写延迟偏移，二者均赋值为 `delay_ms`）。dm 设备名存入 sidecar。
- **clean**: `dmsetup remove` 删除 dm-delay 设备。
- **query**: `dmsetup info/table` 检查 dm-delay 设备是否存在。

**使用示例**:

```bash
dcat inject rDISK_io_delay --device=/dev/sdb --delay_ms=200
dcat query  rDISK_io_delay
dcat clean  rDISK_io_delay
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| device | inject 必填 | 块设备路径 | 目标块设备（如 `/dev/sdb`），必须是未挂载或可安全操作的设备 |
| delay_ms | inject 必填 | 正整数 | IO 延迟毫秒数 |

**危险等级**: 高 — 对正在使用的设备施加延迟可能导致 IO 超时、进程卡顿甚至文件系统损坏。仅在空闲设备上测试。

**补充说明**: 需要 root + `dmsetup`（device-mapper 包）+ 内核 `dm-delay` 模块。**严禁对已挂载的根分区或关键设备执行**。测试前用 `lsblk` 确认目标设备未被使用。

---

### 2.5 rDISK_io_error — IO 错误（dm-error）

**UID**: `rDISK_io_error`

**描述**: 通过 device-mapper `error` 目标使块设备的所有 IO 返回 EIO（输入/输出错误），模拟磁盘故障。

**实现原理**:

- **inject**: 在块设备 `device` 上创建 dm-error 设备 `dcat-error-<devname>`，使用 `dmsetup create` 设置 `error` target。dm 设备名存入 sidecar。
- **clean**: `dmsetup remove` 删除 dm-error 设备。
- **query**: `dmsetup info/table` 检查 dm-error 设备是否存在。

**使用示例**:

```bash
dcat inject rDISK_io_error --device=/dev/sdb
dcat query  rDISK_io_error
dcat clean  rDISK_io_error
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| device | inject 必填 | 块设备路径 | 目标块设备（如 `/dev/sdb`） |

**危险等级**: 高 — 所有 IO 返回 EIO，可能导致文件系统只读、数据损坏。仅在空闲设备上测试。

**补充说明**: 需要 root + `dmsetup` + 内核 `dm-error` 模块。**严禁对已挂载的根分区或关键设备执行**。与 `rDISK_io_delay` 的区别：delay 是延迟 IO，error 是直接返回错误。

---

## 第三章 网络模块（13 条）

本章涵盖 DemonCAT 网络故障注入模块的全部 13 条故障规则。网络模块通过 `tc`（Traffic Control）、`ip`、`iptables`、`systemctl` 及 Python socket 等手段，模拟延迟、丢包、乱序、带宽限制、链路中断、端口占用、服务停止、链路抖动、包损坏、连接耗尽等多种网络异常场景。

> **互斥说明**：基于 `tc qdisc` 的故障（rNET_delay / rNET_loss / rNET_reorder / rNET_bw_limit / rNET_degrade / rNET_jitter / rNET_corrupt）在同一网卡上**互斥**——一个网卡只能有一个 root qdisc。如需在同一网卡注入新故障，必须先 `dcat clean` 恢复旧故障后再注入。若注入失败提示"已有 root qdisc"，执行 `dcat clean --all` 或 `tc qdisc del dev <iface> root` 清理残留后重试。

所有故障均支持 `inject`（注入）、`clean`（清理）、`query`（查询）三个操作。注入时通过 sidecar 临时状态文件（`/tmp/dcat-rNET_*`，用于记录注入前的原始值，便于 clean 时恢复）或 PID 文件记录状态，便于后续清理与查询。

---

### 3.1 rNET_delay — 网络延迟（tc netem）

**UID**: `rNET_delay`

**描述**: 通过 `tc netem` 在指定网卡出向流量上注入固定网络延迟。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem delay <delay_ms>ms`，在网卡根队列上挂载 netem qdisc 并设置固定延迟；将网卡名写入 sidecar 文件 `/tmp/dcat-rNET_delay-<iface>.sidecar`。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `netem.*delay` 匹配判断延迟规则是否生效，命中则退出码 0，未命中则退出码 1。`dcat query` 外层退出码透传脚本结果（0=故障生效，1=未生效），JSON 输出中的 `data.confirmed` 字段与此一致。

**使用示例**:

```bash
dcat inject rNET_delay --iface=eth0 --delay_ms=100
dcat query rNET_delay --iface=eth0
dcat clean rNET_delay --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33、enp0s3 |
| delay_ms | 必填 | 整数 | 延迟毫秒数，如 100 表示 100ms 延迟 |

**危险等级**: 低 — 仅增加网络延迟，不中断连接，不影响其他网卡。但大延迟值可能导致依赖低延迟的应用（如心跳、实时通信）超时。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块；同一网卡若已有 qdisc 或注入了其他 qdisc 故障，则 `tc qdisc add` 会失败，需先 clean 或手动删除；clean 操作会删除网卡上所有 root qdisc，注意与手动配置的冲突。

---

### 3.2 rNET_loss — 网络丢包（tc netem）

**UID**: `rNET_loss`

**描述**: 通过 `tc netem` 在指定网卡出向流量上注入随机丢包。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem loss random <loss_pct>%`，在网卡根队列上挂载 netem qdisc 并设置随机丢包百分比；将网卡名写入 sidecar 文件。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `netem.*loss` 匹配判断丢包规则是否生效。

**使用示例**:

```bash
dcat inject rNET_loss --iface=eth0 --loss_pct=10
dcat query rNET_loss --iface=eth0
dcat clean rNET_loss --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33 |
| loss_pct | 必填 | 整数 | 丢包百分比，范围 0–100，如 10 表示 10% 丢包率 |

**危险等级**: 中 — 丢包率过高会导致 TCP 连接重传甚至超时断开，UDP 应用丢数据，影响所有经过该网卡的流量。建议测试时从低值（1–5%）开始。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块；同一网卡已有 qdisc 或注入了其他 qdisc 故障时 `tc qdisc add` 会失败；clean 会删除网卡上所有 root qdisc。

---

### 3.3 rNET_reorder — 网络乱序（tc netem）

**UID**: `rNET_reorder`

**描述**: 通过 `tc netem` 在指定网卡出向流量上注入包乱序（reorder）。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem delay 10ms reorder <reorder_pct>% 50%`，在网卡根队列上挂载 netem qdisc，内含固定 10ms 延迟作为乱序基准，并按指定百分比和 50% 相关度（correlation）触发包重排；将网卡名写入 sidecar 文件。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `netem.*reorder` 匹配判断乱序规则是否生效。

**使用示例**:

```bash
dcat inject rNET_reorder --iface=eth0 --reorder_pct=25
dcat query rNET_reorder --iface=eth0
dcat clean rNET_reorder --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33 |
| reorder_pct | 必填 | 整数 | 乱序百分比，范围 0–100，如 25 表示 25% 的包会被乱序 |

**危险等级**: 低 — 主要影响 TCP 性能（触发乱序检测与快速重传），通常不中断连接。注意 netem reorder 需配合 delay 参数，脚本内部固定为 10ms 延迟和 50% correlation，不可通过参数修改。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块；乱序的 delay 基准（10ms）和 correlation（50%）为脚本硬编码值，无法通过参数调整。

---

### 3.4 rNET_down — 网卡 down（ip link）

**UID**: `rNET_down`

**描述**: 通过 `ip link set down` 将指定网卡置为 DOWN 状态，模拟网卡链路中断。

**实现原理**: `inject` 执行 `ip link set dev <iface> down`，将网卡链路状态置为 DOWN；将网卡名写入 sidecar 文件。`clean` 从 sidecar 读取网卡名，执行 `ip link set dev <iface> up` 恢复链路并删除 sidecar 文件。`query` 执行 `ip -o link show dev <iface>`，通过匹配 `state DOWN` 判断网卡是否处于 DOWN 状态。

**使用示例**:

```bash
dcat inject rNET_down --iface=eth0
dcat query rNET_down --iface=eth0
dcat clean rNET_down --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33。切勿对管理网卡或 SSH 依赖的网卡执行，否则可能导致失联 |

**危险等级**: 高 — 网卡 DOWN 后该网卡所有 IP 不可达，所有经过该网卡的连接立即中断。若目标为管理网卡或 SSH 所用网卡，将导致远程连接丢失，需通过带外管理或物理终端恢复。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `ip` 命令（iproute2 包）；严禁对管理网卡/SSH 网卡执行；clean 仅恢复链路 UP 状态，不恢复 IP 地址/DHCP/路由等上层配置。

---

### 3.5 rNET_degrade — 网卡降速（tc tbf）

**UID**: `rNET_degrade`

**描述**: 通过 `tc tbf` 限速模拟网卡性能降级。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root tbf rate <speed_mbps>mbit burst <speed_mbps>kbit latency 400ms`，将网卡出向带宽限制为指定速率（默认 10Mbps）；将 `iface speed` 写入 sidecar 文件。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除限速规则，删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，检查是否存在 tbf 规则。

**使用示例**:

```bash
dcat inject rNET_degrade --iface=eth0 --speed_mbps=10
dcat query rNET_degrade --iface=eth0 --speed_mbps=10
dcat clean rNET_degrade --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡，如 eth0、ens33、dummy 网卡 |
| speed_mbps | 可选 | 整数 | 目标速率（Mbps），默认 10。需 ≥ 1 |

**危险等级**: 中 — 限速后网卡带宽大幅降低（如从 1000Mbps 降至 10Mbps），大流量场景下可能导致拥塞、丢包和应用超时。

**补充说明**: 需要 root 权限；依赖 `tc` 命令；与 `rNET_bw_limit` 同为 tc tbf 机制但语义不同（degrade=模拟慢网卡，bw_limit=模拟带宽拥塞）。

---

### 3.6 rNET_port_occupy — 端口占用（socket holder）

**UID**: `rNET_port_occupy`

**描述**: 通过 Python socket 占用指定 TCP/UDP 端口，阻止其他进程绑定该端口，模拟端口冲突。

**实现原理**: `inject` 使用 `python3` 创建 socket，设置 `SO_REUSEADDR`，绑定 `0.0.0.0:<port>`；TCP 模式下调用 `listen(1)`，之后进入 `sleep(3600)` 循环保持占用；以后台进程运行，将 PID 写入 PID 文件。`clean` 从 PID 文件读取进程号，执行 `kill` 终止占用进程并删除 PID 文件。`query` 优先使用 `ss -tulnp`（回退 `netstat -tulnp`）列出监听端口，通过正则匹配判断端口是否被占用。

**使用示例**:

```bash
dcat inject rNET_port_occupy --port=8080 --protocol=tcp
dcat query rNET_port_occupy --port=8080
dcat clean rNET_port_occupy --port=8080
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| port | 必填 | 整数 | 目标端口号，范围 1–65535。占用 1024 以下端口需要 root 权限 |
| protocol | 可选 | 字符串 | 协议类型，`tcp` 或 `udp`，默认 `tcp` |

**危险等级**: 低 — 仅占用单个端口，不影响其他端口的网络通信。但若占用的是关键服务端口（如 80、443、22），则该服务无法启动。

**补充说明**: 依赖 `python3`；不需要 `CAP_NET_ADMIN`，但绑定 <1024 端口需要 root 或 `CAP_NET_BIND_SERVICE`。

---

### 3.7 rNET_service_stop — 服务停止（systemctl）

**UID**: `rNET_service_stop`

**描述**: 通过 `systemctl stop` 或 `pkill` 停止指定网络服务，模拟服务级网络故障。

**实现原理**: `inject` 优先检测 `systemctl` 是否可用：若可用则执行 `systemctl stop <service>`，否则执行 `pkill -x <service>` 按进程名精确杀停；将服务名写入 sidecar 文件。`clean` 从 sidecar 读取服务名恢复启动。`query` 检查服务是否处于 inactive/failed 状态。

**使用示例**:

```bash
dcat inject rNET_service_stop --service=nginx
dcat query rNET_service_stop --service=nginx
dcat clean rNET_service_stop --service=nginx
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| service | 必填 | 字符串 | 服务名称（**不带 `.service` 后缀**，如 `nginx` 而非 `nginx.service`） |

**危险等级**: 高 — 停止关键网络服务（如 sshd、nginx、kubelet）会导致远程管理中断、Web 服务不可用、集群节点异常等。

**补充说明**: 需要 root 权限；严禁停止 sshd 等管理服务以免失联。

---

### 3.8 rNET_link_flap — 链路闪断（ip link 循环）

**UID**: `rNET_link_flap`

**描述**: 通过后台循环执行 `ip link set down/up`，模拟网卡链路反复抖动（link flap）。

**实现原理**: `inject` 启动后台子 shell，循环执行 `ip link set dev <iface> down` → `sleep <cycle_sec>` → `ip link set dev <iface> up` → `sleep <cycle_sec>`，重复 `<count>` 次后自动结束；将子 shell 的 PID 写入 PID 文件。`clean` 从 PID 文件读取进程号并 `kill` 终止循环，删除 PID 文件，并确保网卡恢复 UP 状态。`query` 检查 PID 文件是否存在且对应进程仍存活。

**使用示例**:

```bash
dcat inject rNET_link_flap --iface=eth0 --cycle_sec=2 --count=10
dcat query rNET_link_flap --iface=eth0
dcat clean rNET_link_flap --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡 |
| cycle_sec | 可选 | 整数 | 每次 down/up 的间隔秒数，默认 2 |
| count | 可选 | 整数 | 抖动循环次数，默认 10 |

**危险等级**: 高 — 链路反复 down/up 会导致该网卡上所有连接频繁中断重建，可能触发上层协议重连、HA 脑裂等。严禁对管理网卡执行。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；后台进程以子 shell 形式运行，系统重启后自动停止。

---

### 3.9 rNET_bw_limit — 带宽限制（tc tbf）

**UID**: `rNET_bw_limit`

**描述**: 通过 `tc tbf`（Token Bucket Filter）在指定网卡上注入带宽限速。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root tbf rate <rate_kbps>kbit burst 32kbit latency 400ms`，在网卡根队列上挂载 TBF qdisc，按指定速率限速；将网卡名写入 sidecar 文件。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root` 删除队列规则并删除 sidecar 文件。`query` 执行 `tc qdisc show dev <iface>`，通过正则 `qdisc tbf` 匹配判断限速规则是否生效。

**使用示例**:

```bash
dcat inject rNET_bw_limit --iface=eth0 --rate_kbps=1024
dcat query rNET_bw_limit --iface=eth0
dcat clean rNET_bw_limit --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡 |
| rate_kbps | 必填 | 整数 | 限速速率（KB/s），脚本内部转换为 kbit |

**危险等级**: 低 — 仅限制出向带宽，不中断连接。低速率值会导致大文件传输明显卡顿或超时。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_tbf` 模块。

---

### 3.10 rNET_jitter — 延迟抖动（tc netem）

**UID**: `rNET_jitter`

**描述**: 通过 `tc netem` 在指定网卡出向流量上注入延迟抖动（delay + jitter）。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem delay <delay_ms>ms <jitter_ms>ms`，在网卡根队列上挂载 netem qdisc，设置基础延迟及附加抖动范围；将网卡名写入 sidecar 文件。`clean` 从 sidecar 读取网卡名，执行 `tc qdisc del dev <iface> root`。`query` 检查是否存在两个数值的 delay（delay + jitter）。

**使用示例**:

```bash
dcat inject rNET_jitter --iface=eth0 --delay_ms=100 --jitter_ms=20
dcat query rNET_jitter --iface=eth0
dcat clean rNET_jitter --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡 |
| delay_ms | 必填 | 整数 | 基础延迟毫秒数 |
| jitter_ms | 必填 | 整数 | 抖动范围毫秒数，实际延迟在 [delay-jitter, delay+jitter] 范围内波动 |

**危险等级**: 低 — 主要影响实时音视频等对抖动敏感的应用，通常不中断 TCP 连接。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块。

---

### 3.11 rNET_tcp_loss — TCP 丢包（iptables DROP）

**UID**: `rNET_tcp_loss`

**描述**: 通过 `iptables DROP` 规则在指定端口上注入 TCP 包丢弃，模拟端口级 TCP 丢包。

**实现原理**: `inject` 根据 `direction` 参数在 iptables 中插入 DROP 规则：`in` 方向执行 `iptables -I INPUT -p tcp --dport <port> -j DROP`；`out` 方向执行 `iptables -I OUTPUT -p tcp --sport <port> -j DROP`；`both` 方向同时插入两条规则。`clean` 从 sidecar 读取参数，执行 `iptables -D` 删除对应规则。`query` 检查 iptables 规则是否存在。

**使用示例**:

```bash
dcat inject rNET_tcp_loss --port=8080 --direction=both
dcat query rNET_tcp_loss --port=8080 --direction=both
dcat clean rNET_tcp_loss --port=8080 --direction=both
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| port | 必填 | 整数 | 目标 TCP 端口号 |
| direction | 可选 | 字符串 | 丢包方向：`in`、`out`、`both`（默认） |

**危险等级**: 高 — DROP 规则会导致该端口上所有 TCP 连接的包被静默丢弃，新建连接无法建立、已有连接超时断开。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `iptables` 命令；重启后规则不自动清除。

---

### 3.12 rNET_corrupt — 包损坏（tc netem corrupt）

**UID**: `rNET_corrupt`

**描述**: 通过 `tc netem corrupt` 在指定网卡出向流量上注入随机包损坏（比特翻转）。

**实现原理**: `inject` 执行 `tc qdisc add dev <iface> root netem corrupt <corrupt_pct>%`，在网卡根队列上挂载 netem qdisc 并设置随机包损坏百分比。**若该网卡已有 root qdisc（含手动配置的生产 qdisc/其他故障），注入会失败并被拒绝**，需先 `dcat clean` 或 `tc qdisc del dev <iface> root`，不会静默删除已有规则。`clean` 执行 `tc qdisc del dev <iface> root`。`query` 检查是否存在 `netem corrupt` 规则。

**使用示例**:

```bash
dcat inject rNET_corrupt --iface=eth0 --corrupt_pct=5
dcat query rNET_corrupt --iface=eth0
dcat clean rNET_corrupt --iface=eth0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| iface | 必填 | 网卡名 | 目标网卡 |
| corrupt_pct | 必填 | 整数 | 包损坏百分比，范围 0–100 |

**危险等级**: 中 — 损坏的包会被接收方校验失败并丢弃，导致上层重传。高损坏率等效于高丢包率，但表现更隐蔽（包已发送但内容损坏）。

**补充说明**: 需要 root 权限及 `CAP_NET_ADMIN` 能力；依赖 `tc` 命令及内核 `sch_netem` 模块。与其他 tc qdisc 故障在同一网卡上互斥。

---

### 3.13 rNET_conn_exhaust — 连接耗尽

**UID**: `rNET_conn_exhaust`

**描述**: 向目标 `host:port` 发起大量 TCP 连接并保持不关闭，耗尽目标端的连接资源（模拟连接耗尽攻击）。

**实现原理**: `inject` 使用 Python socket 向 `target`（`host:port` 格式）发起 `count` 个 TCP 连接，建立后保持不关闭，以后台进程运行。PID 写入 pidfile。`clean` 读取 pidfile，kill 持有连接的进程。`query` 检查进程是否存活及 `ss -s` 连接数统计。

**使用示例**:

```bash
dcat inject rNET_conn_exhaust --target=192.168.1.1:80 --count=1000
dcat query rNET_conn_exhaust
dcat clean rNET_conn_exhaust
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| target | inject 必填 | host:port | 目标地址和端口，格式 `IP:port` |
| count | 可选 | 整数 | 发起连接数，默认 1000 |

**危险等级**: 中 — 大量连接占用目标端资源，可能导致目标服务拒绝新连接。对本机测试时也会消耗本机端口和文件描述符。

**补充说明**: 依赖 `python3`。连接耗尽效果取决于目标端的 `somaxconn`、`nf_conntrack_max`、文件描述符限制等。count 过大可能先耗尽本机资源。

---

## 第四章 进程模块（5 条）

### 4.1 rPROC_exit — 进程退出（kill -9，inject-only）

**UID**: `rPROC_exit`

**描述**: 通过 `kill -9`（SIGKILL）强制终止目标进程，操作不可逆。

**实现原理**: inject 对目标 PID 发送 `kill -9`，进程被立即终止且无法恢复。本故障为 inject-only（`supported_ops = inject`），不支持 clean/query。

**使用示例**:

```bash
dcat inject rPROC_exit --pid=12345
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
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
| --- | --- | --- | --- |
| pid | 必填 | 正整数 | 目标进程 PID（clean/query 可省略，从 sidecar 恢复） |

**危险等级**: 中 — 进程被暂停但可由 `SIGCONT` 恢复，可逆。

**补充说明**: 可逆故障，STOP/CONT 成对出现。clean 时可不带参数（从 sidecar 自动恢复 PID）。

> **适用对象**：rPROC_hang 适用于**非终端控制的后台进程**。交互式终端程序（`top`/`vim`/`less`）被 SIGSTOP 后，shell 作业控制会回收终端，clean 的 SIGCONT 使其在后台恢复→试图读终端→被 `SIGTTIN` 再次停止，故无法仅靠 `kill -CONT` 恢复。

---

### 4.3 rPROC_zstate — 僵尸进程（kill 目标 → 僵尸）

**UID**: `rPROC_zstate`

**描述**: 将指定进程 kill 后变为僵尸进程（进程退出但父进程未调用 wait 回收，残留为 Z 状态）。

**实现原理**:

- **inject**: 读取 `DCAT_PARAM_PID` 获取目标进程 PID，记录其父进程 PID（PPID）到 sidecar 文件，然后 `kill -9` 目标进程。进程退出后，如果父进程没有调用 wait 回收，则成为僵尸进程。
- **clean**: 从 sidecar 读取目标 PID 和 PPID。如果僵尸仍存在，kill 父进程使僵尸 reparent 到 init（PID 1），init 自动回收。
- **query**: 检查目标 PID 的 `/proc/<pid>/status` 中 State 是否为 Z。

**使用示例**:

```bash
dcat inject rPROC_zstate --pid=12345
dcat query rPROC_zstate --pid=12345
dcat clean rPROC_zstate --pid=12345
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| pid | 必填 | 正整数 | 目标进程 PID |

**危险等级**: 中 — 会 kill 目标进程和其父进程，操作不可逆。clean 后目标进程已死，需手动重启。

**补充说明**: inject 后如果父进程立即回收子进程，则僵尸不会持续（正常行为）。clean 通过杀父进程强制 reparent 到 init 回收僵尸——如果父进程是关键服务，kill 父进程可能影响其他子进程。

---

### 4.4 rPROC_fork_bomb — fork 炸弹（受控）

**UID**: `rPROC_fork_bomb`

**描述**: 创建 `count` 个子进程（受控版 fork 炸弹），消耗进程表与内存资源。

**实现原理**:

- **inject**: 启动一个 supervisor 进程，在循环中 `fork` 出 `count` 个 `sleep 3600` 子进程。supervisor 的 PID 写入 pidfile，trap SIGTERM 时 kill 所有子进程。
- **clean**: 读取 pidfile，kill supervisor（supervisor 的 trap 会 kill 所有子进程）。
- **query**: 统计 supervisor 的子进程数。

**使用示例**:

```bash
dcat inject rPROC_fork_bomb --count=500
dcat query rPROC_fork_bomb
dcat clean rPROC_fork_bomb
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| count | inject 必填 | 正整数 | 要创建的子进程数量 |

**危险等级**: 高 — 大量进程消耗 PID 表、内存、调度资源，可能导致系统无法创建新进程（fork 失败）。count 过大可导致系统不可用。

**补充说明**: 与经典 fork 炸弹 `:(){ :|:& };:` 的区别：本故障创建固定数量的进程（可控），clean 可全部回收。需注意系统 `ulimit -u`（max user processes）限制。

---

### 4.5 rPROC_fd_exhaust — FD 耗尽

**UID**: `rPROC_fd_exhaust`

**描述**: 单个进程不断打开文件描述符直至达到 RLIMIT_NOFILE（或指定 count），模拟进程级 FD 耗尽。

**实现原理**:

- **inject**: 使用 Python 不断 `os.open('/dev/null')` 打开 FD，直到达到 `RLIMIT_NOFILE` 或指定 `count`。PID 写入 pidfile。
- **clean**: kill 进程，所有 FD 自动关闭。
- **query**: 检查进程存活及 `/proc/<pid>/fd` 下的 FD 数量。

**使用示例**:

```bash
# 耗尽到 RLIMIT_NOFILE 上限
dcat inject rPROC_fd_exhaust --count=1000
dcat query rPROC_fd_exhaust
dcat clean rPROC_fd_exhaust

# 只打开 1000 个 FD
dcat inject rPROC_fd_exhaust --count=1000
dcat clean rPROC_fd_exhaust
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| count | 可选 | 整数 | 要打开的 FD 数量，默认 0（= 直到 RLIMIT_NOFILE） |

**危险等级**: 中 — 被注入的进程无法打开新文件/套接字，但仅影响该进程本身，不影响系统其他进程的 FD 分配。

**补充说明**: 依赖 `python3`。与系统级 `fs.file-max` 耗尽不同，本故障仅耗尽单个进程的 `RLIMIT_NOFILE`。进程级 FD 耗尽时，该进程的 `open()`/`socket()`/`pipe()` 等调用返回 EMFILE。

---

## 第五章 内存模块（4 条）

### 5.1 rMEM_leak — 内存泄漏

**UID**: `rMEM_leak`

**描述**: 分配 `size_mb` 内存并持有不释放，模拟内存泄漏。

**实现原理**:

- **inject**: 使用 perl（`"x" x (size*1024*1024)`）或 Python 持有 `size_mb` 的内存块，进程阻塞在 `sleep` 中。PID 写入 pidfile。
- **clean**: kill 进程，内存自动回收。
- **query**: 检查进程存活及其 RSS（`ps -o rss`）。

**使用示例**:

```bash
dcat inject rMEM_leak --size_mb=2048
dcat query rMEM_leak
dcat clean rMEM_leak
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| size_mb | inject 必填 | 正整数 | 要分配的内存大小（MB） |

**危险等级**: 中 — 持续占用物理内存，减少系统可用内存。size 过大可能触发 OOM killer。

**补充说明**: 优先使用 perl（内存分配效率高），回退 python3。与 `rMEM_oom` 的区别：leak 分配固定大小后持有；oom 持续增长直到 OOM killer 触发。

---

### 5.2 rMEM_oom — OOM

**UID**: `rMEM_oom`

**描述**: 以 `rate_mb` 的速率持续分配内存不释放，直到触发 OOM killer。

**实现原理**:

- **inject**: 使用 perl 或 Python 在循环中**每 0.05 秒分配 `rate_mb` MB**（即约 `20 × rate_mb` MB/秒）内存，持续增长直到 OOM killer 触发杀掉进程。PID 写入 pidfile。
- **clean**: kill 进程（可能已被 OOM killer 杀掉），删除 pidfile。
- **query**: 检查进程存活，或在 `dmesg` 中搜索最近的 OOM-kill 事件。

**使用示例**:

```bash
dcat inject rMEM_oom --rate_mb=64
dcat query rMEM_oom
dcat clean rMEM_oom
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| rate_mb | 可选 | 正整数 | 每 0.05 秒分配的 MB 数（约 `20 × rate_mb` MB/秒），默认 64 |

**危险等级**: 高 — 持续内存分配会导致系统可用内存耗尽，触发 OOM killer 可能杀掉其他关键进程。

**补充说明**: OOM killer 的行为取决于内核的 `oom_score_adj` 和 `oom_score`。被 OOM kill 的进程不一定是本故障的进程，可能是系统中内存占用最大的任意进程。建议在测试环境中使用。

---

### 5.3 rMEM_fragment — 内存碎片

**UID**: `rMEM_fragment`

**描述**: 分配 N 个内存块后释放其中一半，制造内存碎片化（用户空间空洞），模拟内存碎片导致的性能降级。

**实现原理**:

- **inject**: 使用 perl 或 Python 分配 `blocks` 个 `block_kb` KB 的内存块，然后释放偶数索引的块（保留奇数块），形成用户空间碎片化。PID 写入 pidfile。
- **clean**: kill 进程。
- **query**: 检查进程存活及 `/proc/buddyinfo`（内核空闲块分布）。

**使用示例**:

```bash
dcat inject rMEM_fragment --blocks=200 --block_kb=1024
dcat query rMEM_fragment
dcat clean rMEM_fragment
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| blocks | 可选 | 正整数 | 内存块数量，不填则按可用内存自动计算（全部碎片化，预留 1000MB） |
| block_kb | 可选 | 正整数 | 每块大小（KB），默认 1024（1MB） |

**危险等级**: 低 — 用户空间碎片化不直接消耗额外内存（总量 = blocks/2 * block_kb），但可能影响 `malloc` 性能和内核页面分配器的高阶页分配。

**补充说明**: 用户空间碎片化与内核碎片化（buddy allocator）是不同层面。本故障主要制造用户空间碎片，间接影响 `/proc/buddyinfo` 的高阶页可用性。

---

### 5.4 rMEM_swap_overload — Swap 过载

**UID**: `rMEM_swap_overload`

**描述**: 分配 `size_mb`（大于系统可用 RAM）的内存并逐页写入（dirty），强制系统将内存页换出到 swap。

**实现原理**:

- **inject**: 使用 perl 或 Python 分配 `size_mb` 内存，以 16MB 为块逐块写入（touch each page），强制 swap-out。PID 写入 pidfile。
- **clean**: kill 进程，内存自动回收，swap 页释放。
- **query**: 检查进程存活及 `free -m` 中 Swap used。

**使用示例**:

```bash
dcat inject rMEM_swap_overload --size_mb=16384
dcat query rMEM_swap_overload
dcat clean rMEM_swap_overload
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| size_mb | inject 必填 | 正整数 | 要分配的内存大小（MB），应大于系统可用 RAM 才能触发 swap |

**危险等级**: 高 — 大量 swap-out 导致系统性能严重下降（磁盘 IO 飙高、响应延迟增大），可能触发 OOM killer。如果 swap 空间不足则直接 OOM。

**补充说明**: 需要 root 权限（或足够大的 `ulimit -v`）。`size_mb` 应设置得比 `free -m` 的 available 列大，才能有效触发 swap。系统未配置 swap 时无法使用此故障。

---

## 第六章 文件系统模块（2 条）

### 6.1 rFS_file_lock — 文件锁

**UID**: `rFS_file_lock`

**描述**: 通过 `chmod` + `chattr +i` + `mount --bind` 锁定文件，使其不可读/不可写/不可删除（限制对 root 也生效）。

**实现原理**:

- **inject**: 根据 `mode` 参数：
    - `noread`: `chmod a-r` + 对文件 `mount --bind /dev/null`（读取返回空，对 root 也生效；目录仅 chmod）
    - `nowrite`: `chmod a-w` + `chattr +i`（写入失败，root 亦不可写）
    - `norw`: `chmod a-rw` + `chattr +i` + 对文件 `mount --bind -o ro <空文件>`（读取为空，写入返回 EROFS）
    - `nodelete`: `chattr +i`（不可删/改/重命名）
    - 原始 mode、immutable 状态、bind-mount 状态存入 sidecar。
- **clean**: 若 bind-mount 则先 `umount`；若注入了 `+i` 则 `chattr -i`；从 sidecar 恢复原始 mode。
- **query**: 打印当前 mode、lsattr 与 bind-mount 状态。

**使用示例**:

```bash
# 不可读写
dcat inject rFS_file_lock --path=/data/app.conf --mode=norw
dcat query  rFS_file_lock --path=/data/app.conf
dcat clean  rFS_file_lock

# 不可删除
dcat inject rFS_file_lock --path=/data/app.conf --mode=nodelete
dcat clean  rFS_file_lock
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| path | inject 必填；query 可选 | 文件路径 | 目标文件（必须已存在） |
| mode | inject 必填 | 枚举 | 锁定模式：`noread` / `nowrite` / `norw` / `nodelete` |

**危险等级**: 中 — 锁定关键文件可能导致依赖该文件的服务异常。`nodelete`（immutable）需要 root 且文件系统支持 ext attrs。

**补充说明**: `nodelete` 使用 `chattr +i`（需要 root，仅 ext2/3/4、xfs 等支持）。`noread` / `nowrite` / `norw` 对文件额外用 `mount --bind`（需要 root）使限制对 root 也生效；目录仅用 `chmod` + `chattr`（mount 不支持目录）。clean 从 sidecar 恢复原始 mode 并 umount。

---

### 6.2 rFS_iowait_high — iowait 飙高

**UID**: `rFS_iowait_high`

**描述**: 在目标挂载点上启动多个 worker 执行 `dd` 写入+`fdatasync`，推高该文件系统的 iowait。

**实现原理**:

- **inject**: 在 `path`（应为挂载点）下创建临时目录，启动 `workers` 个 worker 循环执行 `dd bs=4k count=100 conv=fdatasync`。PID 写入 pidfile。
- **clean**: kill 所有 worker，删除临时目录。
- **query**: 检查 worker 进程存活及 `mpstat` iowait 统计。

**使用示例**:

```bash
dcat inject rFS_iowait_high --path=/data --workers=4
dcat query rFS_iowait_high
dcat clean rFS_iowait_high
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| path | inject 必填 | 目录路径 | 目标挂载点目录（应为 `mount` 输出第 3 列的挂载点） |
| workers | 可选 | 整数 | dd worker 数量，默认 4 |

**危险等级**: 中 — 高 iowait 影响同文件系统上其他 IO 任务的响应延迟。不会损坏数据。

**补充说明**: 与 `rDISK_write_overload` 的区别：write_overload 面向块设备写吞吐过载；iowait_high 面向挂载点的 iowait 指标。path 应为挂载点而非裸设备。

---

## 第七章 Docker 模块（2 条）

### 7.1 rDOCKER_kill — 容器 kill

**UID**: `rDOCKER_kill`

**描述**: 通过 `docker kill` 强制停止容器；clean 通过 `docker start` 恢复。

**实现原理**:

- **inject**: 执行 `docker kill <container>`，容器被强制停止（SIGKILL）。容器名存入 sidecar。
- **clean**: 从 sidecar 读取容器名，执行 `docker start <container>` 恢复。
- **query**: `docker inspect` 检查容器 State.Status。

**使用示例**:

```bash
dcat inject rDOCKER_kill --container=myapp
dcat query rDOCKER_kill --container=myapp
dcat clean rDOCKER_kill --container=myapp
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| container | inject/clean/query 必填 | 字符串 | 容器名或 ID |

**危险等级**: 高 — 容器内进程被 SIGKILL 立即终止，数据可能丢失。clean 通过 `docker start` 恢复容器，但容器内进程的状态（如内存数据）不可恢复。

**补充说明**: 依赖 `docker` 命令。inject 前检查容器是否存在（`docker inspect`）。与 `docker stop` 的区别：`kill` 发送 SIGKILL（不可拦截），`stop` 发送 SIGTERM（可拦截）后超时再 SIGKILL。

---

### 7.2 rDOCKER_mem_overload — 容器内存过载

**UID**: `rDOCKER_mem_overload`

**描述**: 通过 `docker exec` 在容器内分配 `size` 内存，触发容器 OOM 或内存压力。

**实现原理**:

- **inject**: 通过 `docker exec <container>` 在容器内执行 Python 或 perl 内存持有进程，分配 `size`（支持 `512M`/`2G` 等单位）内存。`docker exec` 的宿主机 PID 写入 pidfile。
- **clean**: kill 宿主机 docker-exec PID（容器内 holder 随之终止）。
- **query**: 检查 docker-exec PID 存活及 `docker stats`。

**使用示例**:

```bash
dcat inject rDOCKER_mem_overload --container=myapp --size=2G
dcat query rDOCKER_mem_overload --container=myapp
dcat clean rDOCKER_mem_overload --container=myapp
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| container | inject/clean/query 必填 | 字符串 | 容器名或 ID |
| size | inject 必填 | 大小字符串 | 要分配的内存大小（如 `512`=`512MB`、`512M`、`2G`） |

**危险等级**: 高 — 容器内内存过载会触发容器 OOM killer（如果设了 `--memory` 限制），或导致宿主机内存压力（如果未设限制）。

**补充说明**: 依赖 `docker` 命令。容器内需有 `python3` 或 `perl`。如果容器设了 `--memory` 限制且 size 超过限制，容器内 OOM killer 会杀掉 holder 进程（故障自动结束）。

---

## 第八章 NPU 模块（20 条）

NPU 模块面向华为 Atlas 系列 NPU 芯片，通过 `hccn_tool`、`npu-smi`、CANN及 PCIe sysfs 对 RoCE 网口、芯片硬件注入连通性、路由、性能与配置类故障。所有脚本共享 `_common.sh`，提供 `npu_check_env`（校验 hccn_tool）、`npu_validate_chip`（校验芯片号）及 sidecar 读写原语（`/tmp/dcat-<uid>-<chip>.bak`）。

> ⚠️ **实机必读**：本章示例中的 `chip`、`dev`、`gateway`、各网段地址均为**机器相关**值——每台机器的 NPU IP、网关、网口名、已用网段都不一样，直接照抄大概率失败。**注入前必须先按下方「8.0 前置参数查询」查出目标机器实际值，再填入各 fault 参数**。下文中 `10.30.12.x / 网关 10.30.12.254 / eth2 / 芯片 2` 是一台 Atlas 8 卡机器的示例拓扑值，仅用于演示查询与换算过程。

### 8.0 前置准备：实机参数查询与调整

所有 NPU 用例注入前，按下列步骤确认目标机器的实际参数。命令中 `chip` 用目标芯片号（Phy-ID，示例取 2，请替换为本机可用芯片号）。

#### ① 确认可注入的芯片与 RoCE 网口名 `dev`

```bash
ls /dev/davinci*                  # 查看可用芯片 Phy-ID（davinci 后的数字）
npu-smi info                      # 查看 NPU 卡拓扑、芯片状态与健康
hccn_tool -i 2 -status -g          # 查询芯片 2 的网口名，输出 "Settings for eth2:" → dev 为 eth2（示例机）
```

> **chip vs npu_id**：多数 NPU 故障使用 `chip`（物理芯片 Phy-ID，`ls /dev/davinci*` 查看）；`pcie_down` 和 `chip_reset` 使用 `npu_id`（NPU 卡号，`npu-smi info` 第一列）。详见 8.0 节⑥。
>
> **关键**：`hccn_tool` 的 `dev` 是 **NPU 内部网卡名**（示例机为 `eth2`，不同机器可能是 `eth0`/`eth2` 等），不是 Linux 系统接口名。用 `hccn_tool -i <chip> -status -g` 可查询，输出首行 `Settings for ethX:` 中的 `ethX` 即为该芯片的 dev 值。

#### ② 查询 NPU IP 与掩码（确定网段）

```bash
hccn_tool -i 2 -ip -g
# 示例输出: ipaddr:10.30.12.9 netmask:255.255.255.0  → 网段 10.30.12.0/24
```

#### ③ 查询当前网关

```bash
hccn_tool -i 2 -gateway -g
# 示例输出 10.30.12.254
```

> 所有涉及 `gateway`/`via` 的注入，该值**必须与 NPU IP 同网段**，否则 `hccn_tool` 报 `segment doesn't match`。

#### ④ 查询路由 / ARP / ip rule / ip route / MTU

```bash
hccn_tool -i 2 -route -g                       # 路由表
hccn_tool -i 2 -arp -g                         # ARP 表
hccn_tool -i 2 -ip_rule -g                     # 策略路由规则
hccn_tool -i 2 -ip_route -g table 100          # 指定路由表的 ip route
hccn_tool -i 2 -mtu -g                         # 当前 MTU
```

#### ⑤ 测试网段选择原则

- `route`/`iproute` 使用的**目标网段**（`address`/`ip`）必须是**目标机器尚未使用、且与 NPU 网段不冲突**的地址，否则查询/断言会误判。
- 这些是**机器相关**参数：在另一台机器上请换成未占用的网段。

#### ⑥ chip 参数语义说明

NPU 故障的 `chip` 参数有两种语义，取决于故障类型：

| 参数名 | 语义 | 范围 | 用于 |
| -------- | ------ | ------ | ------ |
| `chip` | 物理芯片 Phy-ID | 0-15 | hccn_tool 网络类、ACL 负载类（aic/aiv/hbm_load）、driver_unbind、pcie_remove |
| `npu_id` | NPU 卡号 | 0-7 | pcie_down（PCIe 降速，per-card）、chip_reset（芯片复位，per-card） |

- **Phy-ID**：每个物理芯片的唯一编号，对应 `/dev/davinciN`。910B4 每卡 1 芯片（Phy-ID = 卡号）；910C 每卡 2 芯片（Phy-ID 0-15）。
- **NPU 卡号**：`npu-smi info` 中的 NPU ID。PCIe 操作（降速/拔卡/驱动解绑）天然是 per-card。

#### ⑦ device 映射（ACL 负载类自动生成）

`rNPU_aic_load`/`aiv_load`/`hbm_load` 使用 CANN 算子 API 施加负载，需要 Phy-ID→ACL device ID 映射。映射文件位于 `/tmp/dcat-npu-dev-map`，首次运行时**自动生成**，格式为 `<Phy-ID> <ACL-dev-id>`。示例（910B4）：

```text
2 0
5 1
```

表示 Phy-ID 2 = ACL device 0，Phy-ID 5 = ACL device 1。

---

### 8.1 rNPU_link_down — RoCE 链路 down

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
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |

**危险等级**: 高 — 直接切断该芯片所有 RoCE 流量，训练/推理任务全部中断。

**补充说明**: 需要 hccn_tool + Atlas NPU 硬件、需要 root。clean 依赖 `-cfg recovery`，若配置文件缺失可能无法恢复。

---

### 8.2 rNPU_ip_change — RoCE IP 变更

**UID**: `rNPU_ip_change`

**描述**: 修改指定芯片 RoCE 端口 IP 地址与掩码，导致连接中断。

**实现原理**: inject 先 `-ip -g` 取原值存入 sidecar，再 `-ip -s address <addr> netmask <mask>` 覆盖；clean 从 sidecar 还原；query 比对当前 IP 与原值。

**使用示例**:

```bash
dcat inject rNPU_ip_change --chip=0 --address=192.168.1.100 --netmask=255.255.255.0
dcat query rNPU_ip_change --chip=0
dcat clean rNPU_ip_change --chip=0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| address | 必填 | IPv4 | 新 IP 地址 |
| netmask | 必填 | IPv4 | 新子网掩码 |

**危险等级**: 高 — IP 变更后该芯片所有 RoCE 连接立即失效。

---

### 8.3 rNPU_gw_change — RoCE 网关变更

**UID**: `rNPU_gw_change`

**描述**: 修改指定芯片 RoCE 网关地址，导致跨网段路由失效。

**实现原理**: inject 先 `-gateway -g` 取原值存 sidecar，再 `-gateway -s gateway <gw>` 修改；clean 从 sidecar 还原；query 比对当前网关与原值。

**使用示例**:

```bash
# 先查询 NPU IP 网段与当前网关
hccn_tool -i 2 -ip -g          # 示例输出 ipaddr:10.30.12.9 netmask:255.255.255.0
hccn_tool -i 2 -gateway -g     # 示例输出 10.30.12.254

# 注入新网关（必须与 NPU IP 同网段，且≠当前网关）
dcat inject rNPU_gw_change --chip=2 --gateway=10.30.12.1
dcat query rNPU_gw_change --chip=2
dcat clean rNPU_gw_change --chip=2
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| gateway | 必填 | IPv4 | 新网关地址，**必须与 NPU 当前 IP 同网段** |

**危险等级**: 高 — 网关错误后所有跨网段 RoCE 流量无法转发。

**补充说明**: **网关网段匹配**：注入前用 `hccn_tool -i <chip> -ip -g` 查询当前 NPU IP，网关必须在该 IP 的网段内。**no-op 风险**：注入的 `gateway` 若与当前网关相同，不会触发变更。

---

### 8.4 rNPU_netdetect_change — Netdetect IP 变更

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
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| address | 必填 | IPv4 | 新 netdetect 探测地址 |

**危险等级**: 中 — netdetect 失效会导致健康检测误报，影响上层调度。

---

### 8.5 rNPU_arp — ARP 操控

**UID**: `rNPU_arp`

**描述**: 向指定芯片 ARP 表注入伪造 ARP 条目（ARP 毒化），导致流量被误导或停滞。clean 删除注入的条目。

**实现原理**:

- **inject**: 执行 `-arp -a dev <dev> ip <ip> mac <mac>` 添加伪造 ARP 条目，存 sidecar 记录定位键。
- **clean**: 从 sidecar 或参数读取定位键，执行 `-arp -d dev <dev> ip <ip>` 删除条目，rm sidecar。
- **query**: 检查 ARP 表中是否存在 ip+mac。

**使用示例**:

```bash
# ARP 毒化（注入伪造 ARP）
dcat inject rNPU_arp --chip=2 --dev=eth2 --ip=10.30.12.200 --mac=00:11:22:33:44:55
dcat query rNPU_arp --chip=2 --dev=eth2 --ip=10.30.12.200
dcat clean rNPU_arp --chip=2 --dev=eth2 --ip=10.30.12.200
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | inject 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| dev | 必填 | 字符串 | NPU 网卡名（eth 设备名，如 `eth2`，**非** RoCE 子接口名 `end5v0`。用 `hccn_tool -i <chip> -status -g` 确认） |
| ip | 必填 | IPv4 | ARP 条目的 IP |
| mac | inject 必填 | MAC | 伪造的 MAC 地址 |

**危险等级**: 高 — 流量被静默导向错误 MAC，可能导致数据泄漏或连接中断。

**补充说明**: `dev` 是 NPU 内部网口名（机器相关），不是 Linux 系统接口名。

---

### 8.6 rNPU_route — RoCE 路由操控

**UID**: `rNPU_route`

**描述**: 向指定芯片路由表添加路由，可能误导流量走向错误网关或导致网段不可达。clean 删除注入的路由。

**实现原理**:

- **inject**: 执行 `-route -a address <addr> netmask <mask> gateway <gw>` 添加路由，存 sidecar 记录定位键。
- **clean**: 从 sidecar 或参数读取定位键，执行 `-route -d address <addr> netmask <mask>` 删除路由，rm sidecar。
- **query**: 检查路由表是否包含该网段。

**使用示例**:

```bash
# 添加路由
dcat inject rNPU_route --chip=2 --address=10.30.40.0 --netmask=255.255.255.0 --gateway=10.30.12.254
dcat query rNPU_route --chip=2 --address=10.30.40.0 --netmask=255.255.255.0
dcat clean rNPU_route --chip=2 --address=10.30.40.0 --netmask=255.255.255.0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | inject 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| address | 必填 | IPv4 | 目标网段地址（须未使用、不与 NPU 网段冲突） |
| netmask | 必填 | IPv4 | 子网掩码 |
| gateway | inject 必填 | IPv4 | 下一跳网关，**必须与 NPU IP 同网段** |

**危险等级**: 中/高 — 添加错误路由可能将流量导向不可达网关。

**补充说明**: `gateway` 必须与 NPU IP 同网段，否则报 "segment doesn't match"。

---

### 8.7 rNPU_iprule — ip rule 操控

**UID**: `rNPU_iprule`

**描述**: 向指定芯片添加策略路由规则，可能改变流量选路。clean 删除注入的规则。

**实现原理**:

- **inject**: 执行 `-ip_rule -a dir <dir> ip <ip> table <table>` 添加规则，存 sidecar 记录定位键。
- **clean**: 从 sidecar 或参数读取定位键，执行 `-ip_rule -d dir <dir> ip <ip>` 删除规则，rm sidecar。
- **query**: 检查是否同时存在 ip+table。

**使用示例**:

```bash
# 添加 ip rule
dcat inject rNPU_iprule --chip=2 --dir=from --ip=10.30.12.210 --table=150
dcat query rNPU_iprule --chip=2 --dir=from --ip=10.30.12.210
dcat clean rNPU_iprule --chip=2 --dir=from --ip=10.30.12.210
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | inject 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| dir | 必填 | from/to/in/out | 策略匹配方向 |
| ip | 必填 | IPv4 | 策略匹配的源/目的 IP |
| table | inject 必填 | 整数 | 路由表编号 |

**危险等级**: 中 — 受匹配的流量将改走指定路由表，可能改变选路结果。

**补充说明**: 注入前用 `-ip_rule -g` 查看已有规则避免冲突。

---

### 8.8 rNPU_iproute — ip route 操控

**UID**: `rNPU_iproute`

**描述**: 向指定芯片策略路由表添加路由，可能误导流量或导致网段不可达。clean 删除注入的路由。

**实现原理**:

- **inject**: 执行 `-ip_route -a ip <ip> ip_mask <mask> via <via> dev <dev> table <table>` 添加路由，存 sidecar 记录定位键。
- **clean**: 从 sidecar 或参数读取定位键，执行 `-ip_route -d ip <ip> ip_mask <mask> table <table>` 删除路由，rm sidecar。
- **query**: 检查该 table 是否包含指定 ip。

**使用示例**:

```bash
# 添加 ip route
dcat inject rNPU_iproute --chip=2 --ip=10.30.50.0 --ip_mask=24 --via=10.30.12.254 --dev=eth2 --table=100
dcat query rNPU_iproute --chip=2 --ip=10.30.50.0 --ip_mask=24 --table=100
dcat clean rNPU_iproute --chip=2 --ip=10.30.50.0 --ip_mask=24 --table=100
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | inject 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| ip | 必填 | IPv4 | 目标网段地址 |
| ip_mask | 必填 | 整数 0-32 | **CIDR 位数**（如 `24` 表示 /24），不是点分掩码 |
| table | 必填 | 整数 0-255 | 路由表编号 |
| via | inject 必填 | IPv4 | 下一跳地址，**必须与 NPU IP 同网段** |
| dev | inject 必填 | 字符串 | NPU 内部网卡名（用 `hccn_tool -i <chip> -status -g` 确认） |

**危险等级**: 中/高 — 添加路由可能改变选路结果。

**补充说明**: **ip_mask 是 CIDR 位数**（0-32），不是点分掩码。`via` 必须与 NPU IP 同网段。`dev` 是 NPU 内部名（机器相关），不是 Linux 系统接口名。

---

### 8.9 rNPU_bw_limit — RoCE 带宽限速

**UID**: `rNPU_bw_limit`

**描述**: 对指定芯片 RoCE 流量进行带宽限速，降低吞吐。

**实现原理**: inject 执行 `-shaping -s bw_limit <bw>` 设置限速；clean 执行 `-shaping -s bw_limit 100000`（MAX_BW）恢复；query 检查当前 bw_limit 是否小于 MAX_BW。

**使用示例**:

```bash
dcat inject rNPU_bw_limit --chip=0 --bw_limit=1000
dcat query rNPU_bw_limit --chip=0
dcat clean rNPU_bw_limit --chip=0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| bw_limit | 必填 | 整数 (Mbps) | 带宽限速值 |

**危险等级**: 中 — 限速不破坏链路，但显著降低 RoCE 吞吐。

**补充说明**: clean 恢复值为硬编码 MAX_BW=100000，若芯片原始限速非该值则无法精确还原。

---

### 8.10 rNPU_mtu_mismatch — RoCE MTU 变更

**UID**: `rNPU_mtu_mismatch`

**描述**: 修改指定芯片 RoCE MTU，造成 MTU 不匹配引发分片/丢包。

**实现原理**: inject 先 `-mtu -g` 取原值存 sidecar，再 `-mtu -s size <size>` 修改；clean 从 sidecar 还原（缺省 1500）；query 比对当前 MTU 与原值。

**使用示例**:

```bash
hccn_tool -i 2 -mtu -g   # 查询当前 MTU，示例输出 1500
dcat inject rNPU_mtu_mismatch --chip=2 --size=1280
dcat query rNPU_mtu_mismatch --chip=2
dcat clean rNPU_mtu_mismatch --chip=2
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| size | 必填 | 整数 (字节) | MTU 字节数，必须 ≠ 当前 MTU 才有效 |

**危险等级**: 中 — MTU 与对端不匹配导致大包分片或被丢弃。

**补充说明**: **no-op 风险**：注入的 `size` 若与当前 MTU 相同，不会触发变更。注入前用 `-mtu -g` 确认当前值。

---

### 8.11 rNPU_dscp_tc_change — DSCP→TC 映射变更

**UID**: `rNPU_dscp_tc_change`

**描述**: 修改指定芯片 DSCP 到 TC 映射，打乱 QoS 流量分类。

**实现原理**: inject 先 `-dscp_to_tc -g dscp <dscp>` 取原 tc 存 sidecar，再 `-dscp_to_tc -s dscp <dscp> tc <tc>` 修改；clean 从 sidecar 还原；query 比对当前 tc 与原值。

**使用示例**:

```bash
dcat inject rNPU_dscp_tc_change --chip=0 --dscp=46 --tc=0
dcat query rNPU_dscp_tc_change --chip=0 --dscp=46
dcat clean rNPU_dscp_tc_change --chip=0 --dscp=46
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| dscp | 必填 | 0-63 | DSCP 差分服务代码点 |
| tc | 必填 | 整数 0-3 | 流量类编号（hccn_tool 硬限制 0-3，传 4+ 报 `value out of valid range`） |

**危险等级**: 中 — 映射错误使高优先级流量被降级调度，QoS 失效。

---

### 8.12 rNPU_roce_port_change — RoCE UDP 端口变更

**UID**: `rNPU_roce_port_change`

**描述**: 修改指定芯片 RoCE UDP 端口，导致与对端 RoCEv2 通信中断。

**实现原理**: inject 先 `-udp -g` 取原 port 存 sidecar，再 `-udp -s port <port>` 修改；clean 从 sidecar 还原（缺省 4791）；query 比对当前 port 与原值。

**使用示例**:

```bash
dcat inject rNPU_roce_port_change --chip=0 --port=4792
dcat query rNPU_roce_port_change --chip=0
dcat clean rNPU_roce_port_change --chip=0
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| port | 必填 | 1-65535 | RoCE UDP 端口号，默认 4791 |

**危险等级**: 高 — 端口非 4791 时对端按标准 RoCEv2 发包将无法匹配，该芯片所有 RoCEv2 流量中断。

---

### 8.13 rNPU_pcie_down — NPU PCIe 降速

**UID**: `rNPU_pcie_down`

**描述**: 通过 `setpci` 修改 PCIe Link Control 2 寄存器的 Target Link Speed，将 NPU PCIe 链路降速，大幅削减 PCIe 带宽。NPU 在降速后仍可正常访问。

**实现原理**:

- PCIe 链路两端——**Root Port**（CPU 侧 PCIe 控制器，上游）和 **Endpoint**（NPU 设备，下游）——各有独立的 Link Control 2 寄存器。两端 Target Link Speed 都需修改，然后从 Root Port 端触发 Link Retrain 使链路重新协商到新速度。
- **inject**: 从 `npu-smi info -t board` 获取芯片 PCIe BDF，通过 sysfs 找到上游 Root Port。读取两端 LnkCtl2 原始值并保存到 sidecar。将 Target Link Speed 设为目标 Gen 值，在 Root Port 端触发 Link Retrain。
- **clean**: 从 sidecar 恢复原 LnkCtl2 值，重新 Retrain 恢复原速。
- **query**: 检查 sidecar 是否存在，`lspci` 显示当前链路速度。

**PCIe 代数与带宽对照**（原速 Gen4 16GT/s x16）:

| gen | PCIe 代数 | 单通道速度 | x16 总带宽 | 相对原速 |
| ----- | ---------- | ----------- | ---------- | --------- |
| 1 | Gen1 | 2.5 GT/s | ~4 GB/s | 12.5%（降 6.4x） |
| 2 | Gen2 | 5 GT/s | ~8 GB/s | 25%（降 3.2x） |
| 3 | Gen3 | 8 GT/s | ~15.8 GB/s | 50%（降 2x） |

**使用示例**:

```bash
dcat inject rNPU_pcie_down --npu_id=2           # 默认 Gen1，带宽降至 12.5%
dcat inject rNPU_pcie_down --npu_id=2 --gen=2   # Gen2，带宽降至 25%
dcat inject rNPU_pcie_down --npu_id=2 --gen=3   # Gen3，带宽降至 50%
dcat query rNPU_pcie_down --npu_id=2
dcat clean rNPU_pcie_down --npu_id=2
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| npu_id | 必填 | 0-7 | NPU 卡号，`npu-smi info` 表格第一列 NPU ID |
| gen | 可选 | 1-3 | 目标 PCIe 代数（1=Gen1 2.5GT/s，2=Gen2 5GT/s，3=Gen3 8GT/s，默认 1） |

**危险等级**: 中 — PCIe 带宽大幅降低，影响 NPU 数据传输性能。NPU 仍可访问但带宽受限。可逆（clean 恢复原速）。

**补充说明**: 依赖 `setpci`（pciutils 包）。Root Port 和 Endpoint 两端都需设置 Target Link Speed，从 Root Port 端触发 Retrain。

---

### 8.14 rNPU_aic_load — AICore 负载

**UID**: `rNPU_aic_load`

**描述**: 通过 CANN 算子 API `aclnnMatmul` 对指定芯片执行 FP16 矩阵乘法（5120×5120），持续施压 Cube 计算单元，拉高 AICore 使用率。

**实现原理**:

- **inject**: 查找 Phy-ID→ACL device 映射（`/tmp/dcat-npu-dev-map`，自动生成），运行 `build/_npu_stress aicore <dev_id>` 后台进程。固定 50ms PWM 窗口，按 `load_pct` 计算占空比（duty = load_pct / 0.96），满负荷跑 compute 阶段后休眠剩余时间。PID 写入 pidfile。
- **clean**: kill stress 进程。
- **query**: `npu-smi info -t usages` 检查 Aicore Usage Rate。优先查 `Aicore`，无则查 `Aicube`。

> **Aicore vs Aicube 指标差异**：910B4（npu-smi 25.x）的 `Aicore Usage Rate` 即 Cube 单元；910C（npu-smi 26+）的 `Aicore` 是整个 AI Core 流水线的聚合占用率，Cube 专用指标改名为 `Aicube`。query 优先查 `Aicore`，无则查 `Aicube`，适配两个平台。

**负载率原理**：固定 50ms PWM 窗口占空比。`duty = load_pct / 0.96`（满负荷上限约 96%）。每 50ms 窗口内满负荷跑 `duty%` 时间，休眠剩余时间。npu-smi 采样窗口（~1s）内看到 20 个周期，平均值平滑且贴近目标值。

**使用示例**:

```bash
dcat inject rNPU_aic_load --chip=2
dcat inject rNPU_aic_load --chip=2 --load_pct=50   # 50% 负载
dcat query rNPU_aic_load --chip=2
dcat clean rNPU_aic_load --chip=2
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| load_pct | 可选 | 1-100 | 目标负载率百分比，默认 100（满载） |

**负载率原理**：`load_pct` 控制对 NPU 计算单元的占用率。内部实现为自适应占空比：校准系数补偿了 NPU 满载上限（AICore ~96%、AIVector ~84%，因 API 调用开销无法达到 100%）。

> **精度说明**：实测值与设定值的误差通常在 ±2% 以内（中高档 30-90%）。低档（10%）可能因 npu-smi 采样窗口短而产生较大波动；高档（90%+）可能因 usleep 精度限制略低于设定值。`load_pct=100` 时 AICore 实际约 96%、AIVector 约 84%。

**危险等级**: 中 — AICore 持续高负载，影响同芯片上其他训练/推理任务的计算性能。持续运行直到 clean。

---

### 8.15 rNPU_aicpu_load — AICpu 负载

**UID**: `rNPU_aicpu_load`

**描述**: 通过 CANN 算子 API `aclnnTopk` 对指定芯片执行 FP64 Top-K 排序（500×500），持续施压 AICpu 计算单元，拉高 AICpu 使用率。

**实现原理**:

- **inject**: 运行 `build/_npu_stress aicpu <dev_id>` 后台进程。固定 50ms PWM 窗口，按 `load_pct` 计算占空比（duty = load_pct / 0.94），满负荷跑 compute 阶段后休眠剩余时间。PID 写入 pidfile。
- **clean**: kill stress 进程。
- **query**: `npu-smi info -t usages` 检查 Aicpu Usage Rate。

**使用示例**:

```bash
dcat inject rNPU_aicpu_load --chip=2
dcat inject rNPU_aicpu_load --chip=2 --load_pct=50   # 50% 负载
dcat query rNPU_aicpu_load --chip=2
dcat clean rNPU_aicpu_load --chip=2
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| load_pct | 可选 | 1-100 | 目标负载率百分比，默认 100（满载） |

**负载率原理**：同 `rNPU_aic_load`，固定周期 PWM 占空比。AICpu 满载上限约 94%（Topk 算子特性）。

**危险等级**: 中 — AICpu 持续高负载，影响同芯片上其他任务的 AICpu 计算性能。持续运行直到 clean。

---

### 8.16 rNPU_aiv_load — AIVector 负载

**UID**: `rNPU_aiv_load`

**描述**: 通过 CANN 算子 API `aclnnExp` 对指定芯片执行 FP16 元素级指数运算（8192×8192 = 128MB），持续施压 Vector 计算单元，拉高 AIVector 使用率。exp 是计算型算子（多项式逼近），算术强度高于带宽型算子（如 add），能更充分地压满 Vector 单元。

**实现原理**:

- **inject**: 运行 `build/_npu_stress aivector <dev_id>` 后台进程。固定 50ms PWM 窗口，按 `load_pct` 计算占空比（duty = load_pct / 0.84），满负荷跑 compute 阶段后休眠剩余时间。PID 写入 pidfile。
- **clean**: kill stress 进程。
- **query**: `npu-smi info -t usages` 检查 AIVector Usage Rate。

**使用示例**:

```bash
dcat inject rNPU_aiv_load --chip=2
dcat inject rNPU_aiv_load --chip=2 --load_pct=50   # 50% 负载
dcat query rNPU_aiv_load --chip=2
dcat clean rNPU_aiv_load --chip=2
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| load_pct | 可选 | 1-100 | 目标负载率百分比，默认 100（满载） |

**负载率原理**：同 `rNPU_aic_load`，固定周期 PWM 占空比。AIVector 满载上限约 84%（API 调用开销实测校准）。

**危险等级**: 中 — AIVector 持续高负载，影响同芯片上其他任务的 vector 计算性能。持续运行直到 clean。

---

### 8.17 rNPU_hbm_load — HBM 负载

**UID**: `rNPU_hbm_load`

**描述**: 通过 `aclrtMalloc`+`aclrtMemset` 分配并填充指定大小的 HBM 内存并持续持有，占满 HBM 显存空间。

**实现原理**:

- **inject**: 运行 `build/_npu_stress hbm <dev_id> 0 <size_mb>` 后台进程，使用 `aclrtMalloc` + `aclrtMemset` 分配并填充 HBM 内存。PID 写入 sidecar。
- **clean**: kill stress 进程。
- **query**: `npu-smi info -t usages` 检查 HBM Usage Rate。

**使用示例**:

```bash
dcat inject rNPU_hbm_load --chip=2 --size=20G
dcat query rNPU_hbm_load --chip=2
dcat clean rNPU_hbm_load --chip=2
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |
| size | 必填 | 大小字符串 | 分配的 HBM 大小，支持 `2G`/`500M`/`500`（裸数=MB） |

**危险等级**: 中 — HBM 持续高占用，影响同芯片上其他任务的显存分配，可能导致 OOM。持续运行直到 clean。

---

### 8.18 rNPU_chip_reset — 芯片复位

**UID**: `rNPU_chip_reset`

**描述**: 通过 `npu-smi set -t reset` 复位指定 NPU 芯片，模拟芯片级硬件复位。

**实现原理**:

- **inject**: 执行 `printf 'y\n' | npu-smi set -t reset -i <npu_id> -c <core>`（自动确认），芯片被复位，服务中断。
- **clean**: 清除 sidecar 状态（芯片复位后自动恢复，无需手动 clean）。
- **query**: `npu-smi info` 检查 Health 列。

> **风险警告**: 在多芯片卡（如 910C，每卡 2 芯片）上，复位单个芯片可能导致**整张卡的所有芯片同时重启**。生产环境慎用。

**使用示例**:

```bash
dcat inject rNPU_chip_reset --npu_id=2               # 复位卡 2 的 chip 0（默认）
dcat inject rNPU_chip_reset --npu_id=2 --core=1      # 复位卡 2 的 chip 1
dcat query rNPU_chip_reset --npu_id=2
```

> **注**: chip_reset 是一次性故障，不支持 clean。芯片复位后约 5-10 秒自动恢复（Health 回到 OK），无需重启 OS。但多芯片卡（如 910C）上复位一个 chip 可能导致整卡所有 chip 重置。

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| npu_id | 必填 | 0-7 | NPU 卡号，`npu-smi info` 表格第一列 NPU ID |
| core | 可选 | 0-1 | 卡内芯片号，`npu-smi info -t board -i <npu_id>` 输出中的 Chip ID（默认 0） |

**危险等级**: 高 — 芯片复位后该芯片上所有训练/推理任务立即中断，数据丢失。多芯片卡上可能整卡重启。复位后需等待芯片自动恢复（约 3-10 秒）。

**补充说明**: 需要 root + `npu-smi`。inject 使用 `printf 'y\n'` 自动回答 npu-smi 的确认提示。芯片复位后自动恢复，clean 仅清除 sidecar 状态。

---

### 8.19 rNPU_driver_unbind — 驱动解绑

**UID**: `rNPU_driver_unbind`

**描述**: 将 NPU 从 `devdrv_device_driver` 驱动解绑（`echo <pcie_addr> > unbind`），模拟驱动异常/设备失联。

**实现原理**:

- **inject**: 查找 chip 的 PCIe 地址（`npu-smi info` 或 `lspci` + `/dev/davinci*` 回退），`echo <pcie_addr> > /sys/bus/pci/drivers/devdrv_device_driver/unbind`。
- **clean**: `echo <pcie_addr> > bind` 重新绑定，然后执行 FLR（Function Level Reset）。
- **query**: 检查设备是否仍绑定到驱动。

**使用示例**:

```bash
dcat inject rNPU_driver_unbind --chip=2
dcat query rNPU_driver_unbind --chip=2
```

> **恢复方式**：本故障不支持 clean（910B4 上驱动重绑定后固件不完全恢复）。需**温重启（reboot）**才能完全恢复 NPU。

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |

**危险等级**: 高 — 驱动解绑后该芯片不可用，所有 NPU 操作失败。

**补充说明**: 需要 root。PCIe 地址通过 `npu-smi info` 输出中的 `0000:xx:xx.x` 格式获取，或通过 `lspci -D` + `/dev/davinci*` 映射回退。脚本仅支持 `inject,query`，无 clean 分支（910B4 上驱动重绑定后固件不完全恢复，clean 无意义）。

---

### 8.20 rNPU_pcie_remove — PCIe 拔卡

**UID**: `rNPU_pcie_remove`

**描述**: 从 PCIe 总线移除 NPU 设备（`echo 1 > /sys/bus/pci/devices/<addr>/remove`），模拟 NPU 卡物理拔出。

**实现原理**:

- **inject**: 查找 chip 的 PCIe 地址，`echo 1 > /sys/bus/pci/devices/<pcie_addr>/remove`。设备从 PCIe 总线消失。
- **clean**: `echo 1 > /sys/bus/pci/rescan` 重新扫描 PCIe 总线，然后执行 FLR。
- **query**: 检查 NPU 设备是否仍存在于 PCIe 总线。

**使用示例**:

```bash
dcat inject rNPU_pcie_remove --chip=2
dcat query rNPU_pcie_remove --chip=2
```

> **恢复方式**：本故障不支持 clean（910B4 上 PCIe rescan 后设备条目恢复但固件不重新初始化）。需**冷启动（关机再开机）**才能完全恢复 NPU。

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| chip | 必填 | 0-15 | 物理芯片号，`ls /dev/davinci*` 中 davinci 后的数字 |

**危险等级**: 高 — PCIe 设备移除后该芯片完全消失，所有 NPU 操作失败。

**补充说明**: 需要 root。**910B4 恢复行为**：PCIe rescan 恢复设备条目但 NPU 固件不会重新初始化，**需要冷启动（关机再开机）**才能完全恢复 NPU。温重启（reboot）可能不够。与 `rNPU_driver_unbind` 的区别：driver_unbind 仅断开驱动绑定（软件层），pcie_remove 从 PCIe 总线移除设备（更接近物理拔卡）。脚本仅支持 `inject,query`，无 clean 分支（rescan 后固件不恢复，clean 无意义）。

---

## 第九章 系统模块（2 条）

> ⚠️ **危险故障**：本章故障均为 inject-only 且不可逆，会导致系统宕机或关机。仅在测试环境中使用，确保有带外管理或物理访问能力。

### 9.1 rSYS_panic — 内核 panic

**UID**: `rSYS_panic`

**描述**: 通过 sysrq 触发内核 panic（系统立即崩溃重启），inject-only 不可逆。

**实现原理**: inject 执行 `echo c > /proc/sysrq-trigger`（sysrq 'c' = crash），内核立即 panic。系统崩溃后自动重启（如果配置了 `panic=timeout`）。

**使用示例**:

```bash
# 确保 sysrq 已开启
echo 1 > /proc/sys/kernel/sysrq

dcat inject rSYS_panic
```

**参数可选范围**: 无参数。

**危险等级**: 极高 — 系统立即崩溃，所有未保存数据丢失，所有连接中断。系统重启后可恢复。

**补充说明**: inject-only，不支持 clean/query。需要 root + 可写的 `/proc/sysrq-trigger`。如果内核禁用了 sysrq，注入失败。部分内核配置下 sysrq 'c' 可能被屏蔽。

---

### 9.2 rSYS_poweroff — 下电重启

**UID**: `rSYS_poweroff`

**描述**: 关机或重启机器，inject-only 不可逆。

**实现原理**:

- **mode=0**: 执行 `reboot`（或 `shutdown -r now`），系统下电后重启。
- **mode=1**: 执行 `poweroff`（或 `shutdown -h now`），系统下电后保持关机状态（不自动重启）。

**使用示例**:

```bash
# 重启
dcat inject rSYS_poweroff --mode=0

# 关机（不重启）
dcat inject rSYS_poweroff --mode=1
```

**参数可选范围**:

| 参数 | 是否必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| mode | inject 必填 | 0 或 1 | `0`=下电后重启（reboot），`1`=下电后不重启（poweroff） |

**危险等级**: 极高 — 系统立即下电/重启，所有未保存数据丢失，所有连接中断。

**补充说明**: inject-only，不支持 clean/query。需要 root。`mode=0` 后系统重启可恢复；`mode=1` 后需要手动开机或通过 BMC/iDRAC 远程开机。

---

## 第十章 Web 控制台（dcat serve）

### 10.1 概述

`dcat serve` 在二进制内内置 HTTP 控制平面 + 静态前端（`src/web/`），把故障目录、活跃注入、历史记录搬到浏览器。默认**只读**（`--allow-write` 开启注入/清理），无外部 HTTP 依赖。

### 10.2 命令

```bash
# 只读模式（默认），绑定 0.0.0.0（默认全网可访问，浏览器/远程访问即可用；
# 如需仅本机访问，加 --bind 127.0.0.1）
dcat serve --port 8080

# 可写模式（--allow-write 开启注入/清理），绑定所有网卡
dcat serve --port 8080 --bind 0.0.0.0 --allow-write
```

### 10.3 参数

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--port N` | 8080 | HTTP 监听端口 |
| `--bind ADDR` | 0.0.0.0 | 绑定地址（默认 `0.0.0.0` 全网可访问；`--allow-write` 开启写接口后如需对外暴露请自行评估安全，仅需本机访问可用 `127.0.0.1`） |
| `--webroot DIR` | 内置 | 自定义静态前端目录（覆盖内置 `src/web/`） |
| `--allow-write` | 关闭 | 开启注入/清理写操作（默认只读） |

### 10.4 API 端点

| 端点 | 方法 | 说明 |
| --- | --- | --- |
| `/api/state` | GET | 当前活跃注入记录（从磁盘 reload state.json，同步命令行修改） |
| `/api/history` | GET | 历史注入记录（含已清理） |
| `/api/catalog` | GET | 故障目录（含模块/参数声明） |
| `/api/inject` | POST | 注入故障（需 `--allow-write`） |
| `/api/clean` | POST | 清理故障（需 `--allow-write`） |

### 10.5 安全

- `realpath()` 路径穿越防护
- `%2e` URL 编码检测
- `--port` CLI 校验（防止绑定非法端口）
- 默认只读，写操作需显式 `--allow-write`
