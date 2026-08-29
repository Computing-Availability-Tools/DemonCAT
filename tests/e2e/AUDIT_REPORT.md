# DemonCAT e2e testcases.xlsx 全量审计报告

日期：2026-08-29
范围：tests/e2e/testcases.xlsx 全部 645 条用例（TC-001 ~ TC-645）
方法：6 个并行审计 agent 逐条比对 demoncat.conf / src/scripts/* / e2e 框架实现 +
      实机验证（hccn_tool 行为、网段规则、退出码、clean 还原能力）

---

## 0. 关键的实机环境事实（2019.08 已验证）

| 事实 | 值 |
|------|-----|
| NPU 芯片 | 仅 Phy-ID **2 和 5**（/dev/davinci2, /dev/davinci5），其余 chip 不存在 |
| NPU RoCE IP | **10.0.0.99/24**（非旧数据里的 10.30.12.x） |
| RoCE 网关 | 原为无（unset）；测试基线设为 10.0.0.254 |
| hccn_tool | /usr/bin/hccn_tool；网关不可设回"未设置"（无删除操作） |
| 当前 MTU | 1500；netdetect=0.0.0.0；roce_port=4791 |
| dscp 映射 | dscp46→tc0, dscp0→tc0, dscp63→tc0 |
| 已有 ip_rule | `add from 10.20.10.50 table 150`（解释了 xlsx 里幽灵 10.20.10.x 来源） |
| 退出码 | 3=dcat precheck(缺必填/非法格式/未知uid)；1=脚本级校验(范围/hccn拒绝)；5=已注入；0=成功 |
| 网段规则 | route/gw 的 gateway **必须与 NPU 自身 IP 同网段 (10.0.0.0/24)**，否则 hccn "segment doesn't match" |
| ip 冲突 | NPU 自身 IP 不能与宿主/自身 ARP 邻居同段（如 192.168.1.1）；ip_change 测试用 10.0.0.50 |

---

## 1. 审计结论汇总（按类别）

### 1.1 数据错误（~200 处，已修复 ~210 处）

**NPU 网段错误（最高优先，已全部修复）：**
- rNPU_gw_change：`--gateway=10.30.12.1/10.30.12.254` 不在 NPU 网段 → 一律 10.0.0.1（注入目标）/ 10.0.0.254（基线）
- rNPU_route：`--gateway=10.30.12.254` → 10.0.0.254
- rNPU_iproute：`--via=10.30.12.254` → 10.0.0.254
- 验证断言幽灵地址 `10.20.10.3 / 10.20.10.210 / 10.20.10.211` → 实际注入值

**缺失必填参数（inject 因缺参被 dcat precheck 拒，exit 3 而非预期的 1/5/成功）：**
- rNPU_arp 系 inject 缺 `--mac`（inject_required=chip,dev,ip,mac）
- rNPU_route_del 系 inject 缺 `--gateway`
- rNPU_iproute_del 系 inject 缺 `--via/--dev`
- rNPU_iprule_del 系 inject 缺 `--table`

**断言方向颠倒（注入后应 contains、clean 后应 notcontains）：**
- rNPU_arp_del clean 类（TC-263/265）
- rNPU_iproute_del（TC-385/396 notcontains→contains；TC-389 contains→notcontains）
- rNPU_iprule_del（TC-417/428；TC-421）
- rNPU_route_del（TC-502/513；TC-507）

**占位符 vassert `notcontains:<注入值>` / `notcontains:<uid>`（被 eval_assert 判 skip，从不校验）：**
- TC-625/629/631/633 → 填具体值

**异步/slow bug：**
- TC-446（mtu）vcmd="（注入被拒绝）" + vassert exitcode:1 与步骤成功注入矛盾 → 真观测 contains:1280
- TC-297（bw）步骤2 无 dcat 动词不可解析
- TC-296（bw）预期/数据列 bw_limit=1000 越界

### 1.2 设计不合理（~30 条，逐条处理）

**重复 clean 幂等 / clean 后无幽灵 用例前置错误：**
- 前置用"已 clean X" → 无 setup → clean 命中空态 rc1 → `exitcode:1` 首条即过、`state_not_contains` 空转
- 应改前置"已注入 X"构造注入→clean 真实链路（已批量修复）
- 涉及：rNET 系 9 条 + rNPU 系 + rCPU 系

**rNPU_arp_del 系列（TC-259~275）语义漂移：**
- 标题按"del 故障"设计（inject=删除已存 ARP 并存原 MAC），但 arp.sh 实现是 inject=add投毒/clean=del删除
- 已按真实 add/clean 语义修正断言与 --mac

**并发用例（TC-526/527）等无 dcat 命令 → loader 恒 SKIP**

### 1.3 前置条件不合理（~30 条）

- **chip0/chip7 引用**（仅 2/5 存在）→ 全部改 `{chip}`（动态探测）
- **"已注入 arp_del"** 伪 uid → 已改"已注入 rNPU_arp"
- **rNPU_gw/ip/netdetect/roce 前置写 chip0 可用** → 误导（框架实际在检测到的 chip 上跑）
- TC-341"hccn_tool 未安装"在装有 hccn_tool 的机器上构造不出 → 应 SKIP
- TC-344"原网关为 none"依赖机器初始状态（基线已定为 10.0.0.254，none 场景不再成立）

### 1.4 环境不支持（应 SKIP，~30 条）

| 模块 | 原因 | 处理 |
|------|------|------|
| rNPU_link_down 16 条 | RoCE 口 Physically DOWN，`-cfg recovery` 无法恢复 link UP | 保留 SKIP_MODULES |
| rNET_service_stop 8 条 | `--service=nginx` 硬编码，框架只 provision cron/chronyd | 改 `{svc}` 或标记 SKIP |
| CLI/serve 系 | 需 systemd / HTTP 客户端 / mock 二进制 / /opt/dcat 路径 | 标记 manual/SKIP |
| rPROC "父进程 wait" 3 条 | 测试环境父进程不主动 wait，僵尸必持续 | 应 SKIP |
| TC-341 hccn_tool 缺失 | 构造不出 | 应 SKIP |

---

## 2. 生命周期设计（改值型 NPU 故障可重复执行）

### 2.1 问题
改值型故障（gw/ip/netdetect/mtu/dscp/bw/roce）要求 **注入目标值 ≠ 机器当前值**（否则 hccn 回读校验 no-op → "注入回读校验失败:动作未生效"）。

- 第 1 次：原值(none/基线) → 注入 Y → 真变更 → 成功
- clean 后若无法恢复到可还原状态 → 机器停在 Y → 第 2 次注入 Y 变 no-op → 失败

### 2.2 根因
`hccn_tool -gateway` 无"取消/删除"操作 → **网关"未设置"状态不可还原**。clean 遇 orig=none 只能清 sidecar 放任漂移。其余改值型故障（bw/mtu/dscp/roce/netdetect/ip）均可精确还原，无此问题。

### 2.3 设计（已实现并验证）
- **基线原则**：每台测试机的改值型参数必须有确定性可还原基线；网关基线必须为具体值（**10.0.0.254**），注入目标（**10.0.0.1**）≠ 基线
- **收尾（sweep）**：`dcat clean --all`（sidecar 还原原值）+ NPU 基线还原（gateway/ip/bw/dscp/netdetect/roce/mtu 强制回到基线，芯片动态探测，NPU 存在即执行）→ 崩溃残留也能回基线
- **前置守卫（`_npu_target_collisions`）**：改值型故障注入前读 `hccn_tool` 当前值，若目标==当前 → SKIP 并提示（机器漂移可诊断，而非注入阶段报"动作未生效"）
- **验证**：TC-339+TC-347 连续两轮 2 passed ✓

### 2.4 使用
基线可用环境变量覆盖：
```
DCAT_NPU_BASELINE_GW=10.0.0.254 DCAT_NPU_BASELINE_IP=10.0.0.99 DCAT_NPU_BASELINE_NETMASK=255.255.255.0
```

---

## 3. 框架层修复清单（已提交）

| 提交 | 内容 |
|------|------|
| 417452c | NPU 网段/必填参数/断言方向 + SKIP_MODULES 仅保留 link_down + _npu_defaults 补齐 |
| f18033f | 改值型生命周期：sweep 基线还原 + 前置守卫 + NPU_BASELINE_* 常量 |
| dabe6cb | gw 模块残留 10.30.12.1 → 10.0.0.1（补漏） |

---

## 4. 待办（仍需人工决策/低置信度项）

1. rNET_service_stop：nginx 依赖 → 框架 加 {svc} 或标记 SKIP（8 条）
2. rCPU 核号张冠李戴（TC-016/018/605）、pidfile 命名（-0,1.pid vs -c0.pid）、device 三处不一致
3. 重复 clean 用例标题与 exitcode:1 断言自相矛盾（语义统一）
4. CLI/serve/兼容性 系 ~20 条需特定环境 → 标记 manual
5. 前端/describe 类"无 dcat 命令"用例 → 标记 manual 或补可执行命令
