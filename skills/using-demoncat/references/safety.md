# DemonCAT 安全与恢复参考

注入前先看这里。**危险/不可逆故障必须先与用户确认，并想好恢复路径。**

> 本文件只写**稳定**的东西：判定规则、按类别的恢复手段。**当前有哪些**危险故障，以生成目录 [fault-catalog.md](fault-catalog.md) 的危险列（☠️/⚠️）或目标机 `dcat list` 为准——**别在此处硬记 uid**。

## 判定规则（与工具一致）

- **☠️ 不可逆**：故障**不暴露 `clean` 操作**。一旦注入无法 `dcat clean` 还原。
- **⚠️ 高风险**：`dcat list`/`dcat <uid> --help` 标注，或故障**影响连通性/可用性**。

## ☠️ 不可逆类 —— 按类别与恢复

| 类别 | 典型故障 | 恢复方式 |
|---|---|---|
| **进程终止** | 进程 kill（如 `rPROC_exit`） | 由上层（systemd/supervisor）重启，或手动拉起 |
| **NPU 硬件/固件** | 芯片复位、驱动解绑、PCIe 移除 | 复位类多自恢复；解绑/移除类通常**需重启主机或冷启动**（910B4 上固件不保证自动恢复） |
| **系统级** | 内核 panic、关机/重启 | panic 配 `panic=timeout` 会自动重启；关机需带外上电 |

> 具体 uid 见目录危险列的 ☠️ 标记。
>
> **这些故障没有 `query` op，`verify.sh` 会返回 `NOT_VERIFIABLE`**——不能用 `data.confirmed` 判据验证。对它们：**注入前必须与用户二次确认；注入后如实说明"无法自动验证"，绝不谎报成功。**

## ⚠️ 高风险类 —— 按类别与注意

| 类别 | 注意 | 恢复线索 |
|---|---|---|
| **网络连通性**（网卡/链路 down、闪断、停服务、连接耗尽） | 打在管理网卡会切断 SSH；先 `ip route get 8.8.8.8` 确认出口网卡 | 护栏通常拒绝管理网卡；必要时带外恢复 |
| **NPU 网络配置**（改 IP/网关/MTU/RoCE 端口） | 配置漂移，clean 依赖 `-cfg recovery`，损坏时可能无法还原 | MTU 可手动 `hccn_tool ... -mtu -s size 1500` |
| **磁盘**（写满分区 / IO 报错 / inode 耗尽） | 确认目标分区**非系统关键分区** | 删除注入产生的文件/设备映射 |
| **内存**（泄漏 / OOM / swap） | 可能触发 OOM killer 误杀其它进程 | 观测水位，及时 clean |
| **文件系统**（只读/禁写锁） | 确认路径非关键挂载 | clean 会解除 chmod/chattr/mount |
| **容器**（停容器 / 内存压） | 确认容器可恢复 | clean 会重启容器 |
| **CPU 核**（下线 / hang） | 见下方「CPU 核硬约束」 | 复核 `nproc`、`/sys/devices/system/cpu/online` |

### CPU 核硬约束（离线 / 降频 / hang）

- **永不离线或挂起 CPU 0**（引导核，通常也无法离线）。
- **至少保留 1 个在线核**；`cores` 必须**显式指定**，**禁止 `0-$(nproc-1)`（全核）**。
- 注入前后用 `nproc` / `cat /sys/devices/system/cpu/online` **对比展示**，确认可用核未归零。
- 全核或过多核离线会**导致机器 hang**——必须先与用户确认，并准备带外恢复。

## 重注入与 `--force`

- 同资源（同一 `clean_required` 参数）**重复 inject → 退出码 5**。
- 想替换：先 `dcat clean <uid> --参数`；或 `dcat inject <uid> --force`（会**先清掉重叠注入再注入**，慎用）。
- inject-only 故障不支持 clean，其重注行为见各自实现。

## tc 类互斥（网络）

基于 `tc` 的网络故障都挂在网卡 **root qdisc** 上——**一块网卡同一时刻只能有一个**（如延迟/丢包/抖动/损坏/限速/降速）。

- 换注入前先 `dcat clean`，否则 `tc qdisc add` 失败（退出码 1）。
- 残留清理：`dcat clean --all` 或 `tc qdisc del dev <iface> root`。

## 权限

需要 **root** 的模块：网络（tc/ip/iptables/systemctl）、存储（dm/fallocate）、文件系统（mount/chattr）、Docker、NPU、系统。纯用户态：`rCPU_overload`（taskset+perl）、部分内存故障。

## 收尾清单

1. 确认活跃注入：`dcat query`
2. 逐条清理：`dcat clean <uid> --参数`（或 `dcat clean <uid>` 无参）
3. 全清：`dcat clean --all`
4. 复核：`dcat query` 应无活跃记录；网络类可 `tc qdisc show dev <iface>` 交叉确认
