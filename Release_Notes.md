# DemonCAT Release Notes

> 本文档记录每次发布的版本信息。每次发布在顶部追加，不删除历史记录。

---

## v0.1.0

| 项目 | 说明 |
|------|------|
| 版本号 | v0.1.0 |
| 发布时间 | 2026-08-03 |
| 平台支持 | Linux (aarch64 / x86_64), WSL 兼容 |

### 变更摘要

**NPU 真机验证（Atlas 910B4）：**
- 19 条 rNPU_* 故障全部通过真机 inject/clean/query 验证（device 2 & 5）
- 4 种清理策略（reverse op / sidecar replay / set to max / cfg recovery）真机全覆盖
- `link_down.sh` 修复：`hccn_tool -link -s down` 交互式 y/n 确认 → `echo y |` 自动应答
- NPU inject 回读：fault_present() 条件化，inject 参数降为可选（query 场景）
- `prio_tc` / `pfc` inject 前检查 link 状态，link DOWN 时拒绝注入避免故障卡死

**故障目录调整（37 → 36 条）：**
- 删除 `rNPU_fec_change`：910B4 硬件不支持 FEC 模式切换（`hccn_tool -fec` 返回 "This device does not support switching fec mode"）
- NPU 模块从 20 条减至 19 条

**NPU query 增强：**
- `npu_foreach_chip`：无参 query 遍历所有 NPU device（与 CPU/网络模块行为一致）
- 9 个 fault_present 加 sidecar 回退：无参 query 时用 sidecar 存在性判断故障状态
- 4 个 add 类故障（arp_poison/route_add/iprule_add/iproute_add）inject 加 sidecar_save，clean 加 sidecar_clear
- query 输出加 `FAULT CONFIRMED` / `FAULT NOT ACTIVE` 文本提示
- `confirmed` 字段修复：echo 退出码恒 0 导致 dispatch.c 误判 → 加 `false` 让 NOT ACTIVE 返回 1

**Bug 修复（8 个）：**
- `net_delay.sh` / `net_jitter.sh`：query 正则 `[0-9]+` → `[0-9.]+` 匹配 tc 小数输出
- `ip_change.sh`：fault_present grep 子串匹配 → 精确 IP 值比较；静态 SIDECAR → 动态 SIDECAR_FN()
- `bw_limit.sh`：clean 用未定义变量 `$MAX_BW` → 改为 sidecar 的 `$orig`
- `proc_hang.sh`：pid=0 导致 `kill -STOP 0` 停掉整个进程组 → 加正整数校验
- `disk_write_overload.sh`：symlink 攻击防护（`readlink -f` 检查拒绝 symlink）+ clean 先 SIGTERM 再 SIGKILL 防 dd 孤儿 + workers/size_mb/device 输入校验
- `net_degrade.sh`：从 ethtool 改为 tc tbf（物理+虚拟网卡均可测）
- `gw_change.sh`：无原网关时 sidecar 未保存 → 始终保存 `${orig:-none}`
- `cpu_overload.sh`：query 按 `--cores` 参数过滤，不再显示其他核进程

**输入校验增强：**
- `net_loss.sh`：loss_pct 加 0-100 范围校验
- `net_delay.sh`：delay_ms 加正整数校验

**E2E 测试框架（358 条）：**
- 8 类混沌工程测试矩阵：FUNC / BOUND / SEC / STATE / RES / CLI / CONC / INTER
- NPU 真机适配：chip=0→2、IP 网段修正、grep 正则匹配 hccn_tool 真机输出
- sweep 加 NPU stale state 条件清理（ip_rule/route/ip_route/link up）
- `run_e2e.py` 修复：CONC `& wait` 和 SEC `; rm` 的 shell 语法在 shlex.split 下不生效
- 新增 27 条用例：NPU FUNC-Q（query\<uid\> confirmed）、NPU BOUND（bw_limit/size/port/dscp）、NPU RES（clean --all）、NPU STATE（reinject 拒绝）

**合并：**
- upstream PR #20: state record_id 64-bit 防 overflow + started_at 可读格式
- upstream PR #21: rCPU_core_offline 校验核心实际状态
- upstream PR #22-26: clean --all / 无参 clean / state reconcile / e2e 测试框架

### 已知限制

- **RoCE 链路需物理连接**：`rNPU_link_down` / `rNPU_prio_tc_change` / `rNPU_pfc_change` 在 RoCE 网口未连接交换机时无法完整验证。link DOWN 时 `-cfg recovery` 不会拉起物理链路；`prio_tc` / `pfc` 的 `-g` 读取依赖 link UP。需 link UP 环境做完整 down→up 循环验证。
- `rNPU_route_clear`：`hccn_tool -route -c` 返回 success 但路由表未清空（910B4 驱动限制）
- NPU 19 条故障需 Atlas 物理机 + `hccn_tool` 真机验证
- 网络 11 条故障依赖 root 权限（`tc` / `iptables` / `ip` / `systemctl`）

---

## v0.1

| 项目 | 说明 |
|------|------|
| 版本号 | v0.1 |
| 发布时间 | 2026-07-25 |
| 发布人 | SamWongCc |
| 平台支持 | Linux (x86_64), WSL 兼容 |

### 变更摘要

**核心框架（9 模块 + 插件架构）：**
- `cli` / `config` / `registry` / `executor` / `precheck` / `state` / `dispatch` / `output` / `help` + `main`
- 动态插件架构：`plugin.h` + `plugin_manager.c`（dlopen + ABI 版本检查 + lifecycle）
- 3-tier dispatch：cnf 故障 → 编译注入器 → 动态插件（dlopen .so）

**命令格式：**
- 子命令式 `dcat <subcommand> [uid] --key=value ...`
- 支持 `inject` / `clean` / `query` / `list` 四个操作
- `--help` 子命令感知帮助（`dcat inject --help` 列出所有支持 inject 的故障及参数）
- `--config <path>` / `--plugins <dir>` 全局选项

**per-operation 参数声明：**
- conf 按操作分别声明必填/可选参数：`inject_required` / `inject_optional` / `clean_required` / `clean_optional` / `query_required` / `query_optional`
- 空字段可省略（默认空字符串）
- precheck 按操作检查各自的 `*_required`，错误提示带参数名
- `dcat query` / `dcat clean` 必须指定 uid + 对应参数

**故障目录（37 条）：**
- CPU 2 条：核满载（可调负载 1-100%）、核离线
- 存储 1 条：磁盘写压
- 网络 11 条：延迟 / 丢包 / 乱序 / 网卡 down / 降速 / 端口占用 / 服务停止 / 链路闪断 / 带宽限制 / 抖动 / TCP 丢包
- 进程 3 条：进程退出（inject-only）/ 进程挂起 / 僵尸进程
- NPU 20 条：RoCE 链路 / IP / 网关 / ARP / 路由 / 策略路由 / 带宽 / MTU / FEC / DSCP / PFC / RoCE 端口
- 全部以外部脚本 + 声明式配置接入，加一个故障 = 加一个脚本 + conf 一段，免重新编译

**Bug 修复：**
- `state_add` 返回值检查（满表时不写 record_id，返回错误）
- `state_save` cJSON NULL check + fputs/fclose 返回值检查 + I/O 移出锁范围
- `executor` clear_stale_env_params 防 clean 循环 setenv 泄漏
- `_POSIX_C_SOURCE=200809L` 确保 strict C11 可移植
- NPU 脚本 `npu_validate_chip` 防命令注入
- `disk_write_overload` trap 杀 dd 子进程防孤儿
- `cpu_overload` query 只显示指定核的 mpstat
- `cpu_overload` cores/load_pct 输入校验
- `rPROC_zstate` 重新设计：kill 真实进程制造僵尸（参数从 count 改为 pid）

**测试体系（22 CTest + root 冒烟）：**
- Tier 0 核心单元测试（13 个）：types / output / config / registry / executor / precheck / state / injectors / dispatch / cli / faults / help / plugin_manager + plugin_integration
- Tier 1 mock 表驱动故障测试（37 条全覆盖）
- Tier 2 脚本语法检查（sh -n 全部 37 脚本 + _common.sh）
- Tier 3 真实执行测试（6 条非 root 故障）
- root 冒烟测试（smoke_root.sh，10 条可测 + 3 条跳过）

**文档：**
- README（含依赖说明 + 一键安装脚本 `scripts/install_deps.sh`）
- 用户手册 `docs/user_manual.md`（37 条故障 × 7 字段，含目录）
- 手动测试指南 `docs/manual_test_guide.md`
- SPEC（技术规格）+ DESIGN（架构设计）
- Release Notes + docs/test_report.md

**构建：**
- CMake ≥ 3.10，C11（`_POSIX_C_SOURCE=200809L`），`-Wall -Wextra -Werror`
- cJSON vendored + pthread + dlopen（`${CMAKE_DL_LIBS}`）
- 示例动态插件 `plugins/libsample.so`

### 已知限制

- NPU 20 条故障需 Atlas 物理机 + `hccn_tool` 真机验证
- 网络 11 条故障依赖 root 权限（`tc` / `iptables` / `ip` / `ethtool` / `systemctl`）
- `rNET_degrade` 在 dummy 虚拟网卡上不支持（需真实物理网卡）
- 不实现超时自动恢复：所有可恢复故障注入后需用户手动 clean
- 不实现安全确认交互：预检仅做静态校验
- D 状态故障（rPROC_dstate）暂不实现：D 状态为内核 I/O 调度层状态，无法从用户态可靠注入
- 编译注入器 `builtin_injectors[]` 为空：所有故障均走 cnf + 脚本路径
