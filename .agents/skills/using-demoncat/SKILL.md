---
name: using-demoncat
description: >-
  Use whenever the user wants to inject, simulate, list, query, or clean faults on a Linux or Ascend host with DemonCAT (the `dcat` CLI) — network delay/loss/reorder/jitter/bandwidth, CPU overload/offline, memory leak/OOM, process kill/hang, disk fill/IO delay, filesystem locks, Docker container faults, NPU RoCE/HBM/AICore faults, or any chaos / resilience drill. Triggers on "dcat", "DemonCAT", "故障注入", "混沌演练", "故障演练", "模拟网络抖动", "让 CPU 跑满", "内存泄漏", "清理故障", "拉高 iowait", or any request to deliberately degrade or break a running system to test its resilience.
license: Apache-2.0
compatibility: >-
  Target Linux host with the DemonCAT `dcat` CLI installed. NPU faults additionally
  require an Ascend host with hccl_tool / npu-smi; network, disk, filesystem, Docker
  and system faults generally require root. The `dcat` commands run on the compute
  node; an agent node may drive them remotely over SSH / Ansible.
metadata:
  author: Computing-Availability-Tools
  upstream: https://github.com/Computing-Availability-Tools/DemonCAT
  role: fault-injection
---

# 使用 DemonCAT 注入故障

## 概述

DemonCAT（`dcat`）是一个**声明式故障注入 CLI**：把"想要的故障"翻译成 `dcat inject <uid> --参数=值`。故障以**外部脚本 + 配置**接入，覆盖 CPU / 内存 / 存储 / 网络 / 进程 / 文件系统 / Docker / NPU / 系统 九个模块。

核心心智模型：**inject 注入 → query 验证 → clean 清理**。多数故障可逆，少数**不可逆**（见「安全红线」）。

> `description` 里的关键词只用于**触发本 skill**；**当前有哪些能力一律以 `dcat list` 为准**——绝不从 description 措辞反推某个 uid 存在。

## 何时用 / 何时不用

**用**：用户想**故意制造**故障来测试系统韧性——网络延迟/丢包/抖动、CPU 过载/降频、内存泄漏/OOM、磁盘写满/IO 慢、进程 kill/挂起、NPU RoCE/HBM/AICore 异常，或泛泛的"混沌演练/故障演练"。

**不用**：用户想**排查**一个已经存在的故障（那是运维诊断，不是注入）；或对生产系统的未授权破坏。

## ⚠️ 铁律：没有验证证据，不得声称"成功"

这是本 skill 最重要的一条。故障注入**极易产生错觉**——模型可能报告"注入成功"，而实际没注入、注入了别的、或能力根本不存在。**唯一可信的成功判据是可观测证据**：

1. **成功 = `dcat query <uid> --参数` 输出 JSON 里 `data.confirmed` 为 `true`。** ⚠️ **`dcat query` 的退出码 0 只代表"命令跑通"，不代表故障生效**（故障未注入时它也返回 0）；`inject` 退出 0 更不等于成功——**只认 `data.confirmed`**。
2. **能力存在性以 `dcat list` 为准**。目录里没有的故障 = **没有这个能力**，如实告诉用户，**绝不编造** uid 或参数。
3. **报告必须附证据**：每条结论后给出 **① 确切命令 ② 关键输出原文**（query 的 JSON 里 `confirmed` 值）。不得只写"已成功"，不得概括或臆测。
4. **退出码如实转述**：3=预检拒绝、4=未找到、5=重注入拒绝 —— 这些**不是**成功。
5. **inject-only 故障（无 `query` op，如进程 kill / 系统 panic / 关机）无法用 `query`/`verify.sh` 验证**——它们**不可逆**，只能如实说"已注入、无法自动确认、请你二次确认"，**绝不谎报"验证成功"**（`verify.sh` 对它们返回 `NOT_VERIFIABLE`）。

### 取证脚本（优先用它出证据）

```bash
# 用 sh 调用 + 给 skill 目录下的路径：既不依赖可执行位，也不依赖当前目录
sh <本 SKILL.md 所在目录>/scripts/verify.sh <uid> [--<参数>=<值> ...]
```

首行是裁决（其后是 `dcat query` 原始输出，一并贴出）：
- `CONFIRMED` = 已生效；`NOT_ACTIVE` = 未生效；
- `NOT_VERIFIABLE` = inject-only（无 `query` op），**无法验证，别当成"未生效"**；
- `NOT_FOUND` = uid 不存在（`dcat list` 核对）；`ERROR` = query 本身失败（如参数写错），看原始输出。

> `sh` 调脚本两种失败：`126`=脚本无 `+x`；`127`（`sh: cannot open`）=路径不对（当前目录 ≠ skill 目录）。给全路径即可规避。

### Red flags（出现即停，去核实）

| 你正想说/想做 | 实际必须做 |
|---|---|
| "我记得 uid 是 `rXXX`" | 先 `dcat list` 核对，别凭记忆 |
| "inject 没报错，所以成功" | 跑 `dcat query` / `verify.sh` |
| "这个能力应该有吧" | `dcat list` 查；没有就明确说**没有** |
| "query 输出看着对" | 贴**原文 + 退出码**，不要描述性概括 |
| "注入的应该是同一个故障" | `dcat query`（无 uid）列出**全部活跃**，核对恰好是你注入的那条 |
| "差不多的故障也行" | 注入**用户要的**那条；要替换先 clean，别偷换 |

### 独立探针（工具之外的旁证，防"注入了别的故障"）

| 域 | 旁证命令 | 期望 |
|---|---|---|
| 网络延迟/丢包/抖动 | `tc qdisc show dev <iface>` | 出现 `netem ...` |
| 网卡 down | `ip -br link show <iface>` | 状态为 `DOWN` |
| CPU 打满/下线/限频 | `nproc`、`cat /sys/devices/system/cpu/online` | 在线核数/清单发生变化 |
| 进程 kill/hang | `ps -o pid,stat -p <pid>` | 无该 pid，或状态 `T` |
| NPU | `npu-smi info` | 对应 chip 的占用/健康变化 |
| 磁盘写满 | `df -h <path>`、`lsblk` | 使用率上升 |

## 请求的故障不存在时（显式协议）

用户要的能力若不在目录里（`dcat list` 查无此 uid）：

1. **核对**：`dcat list` 是唯一事实来源——确认确实不存在。
2. **如实告知**：明确说"DemonCAT 没有这个故障"，**绝不编造 uid/参数**。
3. **给最近的替代**：按意图列出最接近的现有能力（例："显存类只有『HBM 压力注入』这一种能力，没有 HBM ECC/多比特错误注入"）——具体 uid 用 `dcat list` 查。
4. **禁止静默替换**：只有**用户明确同意**后才能用替代故障注入，不得自作主张换一个"差不多的"。

## 命令骨架

```bash
dcat list                                   # 列出全部故障（uid + 模块）  ← 发现能力的唯一来源
dcat inject <uid> --<参数>=<值> ...         # 注入（同步阻塞执行）
dcat query  [uid] [--<参数>=<值> ...]       # 验证：无 uid 列全部活跃；有 uid 验证是否生效
dcat clean  <uid> [--<参数>=<值> ...]       # 按资源参数匹配清理
dcat clean  <uid>                           # 无参：清该 uid 全部工件（stateless）
dcat clean  --all                           # 清全部故障（stateless fan-out）
dcat inject <uid> --help                    # 看该故障的准确参数
dcat serve  [--port 8080] [--allow-write]   # Web 控制台（默认只读）
```

> 未安装时用 `./build/dcat` 代替 `dcat`。多数网络/磁盘/NPU/系统故障需 **root**。

## 远程执行（SSH / Ansible 式）

`dcat` 命令都**在计算节点上执行**。若你从**控制 / agent 节点**远程下发（SSH / Ansible），命令本身不变，只是包一层，注意三点：

1. **要 root**：多数故障需要；Ansible 加 `--become`（`-b`）。
2. **看 `dcat` 的退出码，别只看 Ansible 的成败**：`dcat` 的 2/3/5（解析 / 预检 / 重注入）会被 Ansible **默认判为失败**——用 `shell:` + `register:` + `failed_when: false`，再按 `rc` 与 stdout 判断（ad-hoc 同理）。
3. **验证必须在同一节点**：`dcat query` / `scripts/verify.sh` 也要在**那台计算节点**上跑，看 `confirmed:true`；**别在控制节点本地查**。

```bash
# Ansible ad-hoc（command 模块默认把非零 rc 当失败，务必按第 2 点处理）
ansible <host> -b -a "dcat inject rNET_delay --iface=eth0 --delay_ms=200"
ansible <host> -b -a "dcat query  rNET_delay --iface=eth0"     # confirmed:true 才算生效
ansible <host> -b -a "dcat clean  rNET_delay --iface=eth0"
```

## 按模块找故障（**uid 与参数以 `dcat list` 为实时准**）

先用 `dcat list` 拿到当前全部故障与参数。要按意图把问题落到模块/uid 前缀，查 [references/modules.md](references/modules.md)；每个故障的命令示例见 [references/quick-commands.md](references/quick-commands.md)；完整参数与危险标注见 [references/fault-catalog.md](references/fault-catalog.md)。**这三份都是生成物，随工具发版自动更新——本 SKILL.md 不含任何会变的 uid/参数清单。**

## 退出码（据此判断结果）

| 码 | 含义 | 怎么处理 |
|---|---|---|
| 0 | 成功 | 仍需 `query` 确认故障**生效** |
| 1 | 运行错误（脚本失败） | 读 stdout/stderr 定位（缺 root？缺工具？） |
| 2 | 命令格式不合法 | 检查子命令 / 参数格式 |
| 3 | **预检拒绝**（缺参 / 参数非法 / op 不支持） | 补必填参数、修正越界值，或该故障不支持该 op |
| 4 | 未找到（uid 不在目录） | uid 拼错 → `dcat list` 核对 |
| 5 | **重注入拒绝**（同资源已注入） | 先 `dcat clean <uid> --参数`，或加 `--force` 原子替换 |
| 126 / 127 | **运行脚本失败**（shell 报错，非 dcat）：无 `+x` / 路径不对 | 用 `sh <全路径>` 调用 |

> ⚠️ **`dcat query` 特例**：退出码 **0 只表示命令执行成功，不代表故障生效**（未注入时也返回 0）；生效与否看输出 JSON 的 `data.confirmed`。uid 不存在返回 **4**。

## 安全红线（必须遵守）

> 判定规则**不写死 uid**——危险是故障的属性，以工具为准：**☠️ = 该故障没有 `clean` 操作**（自动推导，生成目录危险列标 ☠️）；**⚠️ = `User_Manual.md` 危险等级为「高/极高」**（生成目录危险列标 ⚠️），或故障影响连通性/可用性。

- **☠️ 不可逆（无 clean）**：不暴露 `clean` 的故障一旦注入**无法 `dcat clean` 还原**。执行前**必须**向用户确认，并想好恢复手段（按类别见 [references/safety.md](references/safety.md)）。
- **⚠️ 影响连通性/可用性**：把网卡或链路弄 down、改 IP/网关/MTU/RoCE 端口、停服务、写满分区/耗尽 inode、耗尽内存或连接、锁文件——执行前先与用户确认。
- **⚠️ 可能失联**：**把网卡或链路置 down 的故障**若打在**管理网卡 / 默认路由网卡**上会切断 SSH。先 `ip route get 8.8.8.8` 查出口网卡——输出里 `dev <iface>` 就是当前 SSH 走的网卡，**别对它注入**。脚本另有护栏（检测到默认路由网卡会**拒绝并返回 1**）；确需对管理网卡操作时，先准备**带外 / 物理终端**恢复。**切断 NPU 网络的链路类故障**同理。
- **tc 类互斥**：**基于 `tc` 的网络故障**（延迟/丢包/乱序/抖动/损坏/限速/降速类）都挂在同一网卡 root qdisc，**一块网卡同一时刻只能有一个**；换注入前先 `dcat clean`。具体哪些基于 `tc`，以脚本 / `dcat <uid> --help` 为准。
- **重注入**：同资源重复 inject 返回 **5**；要替换先 clean，或加 `--force`（`--force` 会**先清掉重叠注入**，谨慎）。
- **收尾清理**：观测完**主动** `dcat clean`；不确定有哪些活跃注入时用 `dcat query`；收尾 `dcat clean --all`。

## 标准工作流

```
- [ ] 1. 确认能力与参数：`dcat list`（uid 是否存在）/ `dcat inject <uid> --help`
- [ ] 2. 注入：`dcat inject <uid> --参数=值`（危险故障先与用户确认 + 想好恢复手段）
- [ ] 3. 验证：`sh <本 SKILL.md 所在目录>/scripts/verify.sh <uid> --参数`（或 `dcat query`）→ 要 `CONFIRMED` / `confirmed:true`（inject-only 故障走不了这步，见铁律 5）
- [ ] 4. 出证据：贴「命令 + 关键输出原文」
- [ ] 5. 观测 / 实验（用户进行）
- [ ] 6. 清理：`dcat clean <uid> --参数`，再用 `<本 SKILL.md 所在目录>/scripts/verify.sh`（或 `dcat query`）确认已恢复
```

## 常见错误

- **uid 拼错 / 编造不存在的 uid** → 退出码 4。先 `dcat list` 核对；uid **大小写敏感**，直接照抄 `dcat list` 里的原文。
- **把 `inject` 退出 0 当成成功** → 必须 `query` 确认。
- **`cores` 写 `all` / `*`** → 不支持，必须显式 `0-3` / `0,2,4`（全部核用 `0-$(nproc-1)`）。
- **clean 传参不匹配** → 清不到记录；最省心是无参 `dcat clean <uid>`（清该 uid 全部工件）。
- **忘了 clean 又重注** → 退出码 5。
- **在管理网卡上注入 down 类网络故障** → 被护栏拒绝（1），换非管理网卡。
- **无 root 跑网络 / 磁盘 / NPU 故障** → 退出码 1。

## 参考

均为随工具发版自动更新的**生成物**（本 SKILL.md 本身不含会变的数据）：

- 模块与 uid 前缀：[references/modules.md](references/modules.md)
- 每个故障的命令示例：[references/quick-commands.md](references/quick-commands.md)
- 完整参数 + 危险标注：[references/fault-catalog.md](references/fault-catalog.md)
- 危险/不可逆故障的恢复流程（稳定，按类别）：[references/safety.md](references/safety.md)
- 数据生成器（从工具重新生成以上三份）：[scripts/gen-catalog.js](scripts/gen-catalog.js)
- DemonCAT 仓库手册（若可得）：`User_Manual.md`、`docs/DemonCAT_Error_List.md`、`SPEC.md`
