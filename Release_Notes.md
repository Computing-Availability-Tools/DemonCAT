# DemonCAT Release Notes

> 本文档记录每次发布的版本信息。每次发布在顶部追加，不删除历史记录。

---

## v0.1.0

| 项目 | 说明 |
| ------ | ------ |
| 版本号 | v0.1.0 |
| 发布时间 | 2026-08-11 |
| 发布人 | sunnytao |
| 平台支持 | Linux (aarch64 / x86_64)，NPU 故障需华为昇腾 Atlas 服务器 |
| 许可证 | Apache-2.0 |

### 版本定位

DemonCAT 的初始开源版本。覆盖 CPU / 存储 / 网络 / 进程 / NPU / Docker / 文件系统 / 系统模块，提供统一的命令面、预检护栏、状态跟踪；58 条故障以外部脚本 + 声明式配置接入，加一个故障 = 加一个脚本 + 配置文件一行，免重新编译。

### 变更摘要

#### 核心框架

- `cli` / `config` / `registry` / `executor` / `precheck` / `state` / `dispatch` / `output` / `help` + `main`，共 9 模块
- 动态插件架构：`plugin.h` + `plugin_manager.c`（`dlopen` + ABI 版本检查 + lifecycle）
- 3-tier dispatch：cnf 故障 → 编译注入器 → 动态插件（`dlopen .so`）
- 示例动态插件 `plugins/libsample.so`

#### 故障目录（58 条）

CPU 2 / 存储 1 / 网络 11 / 进程 3 / NPU 16，按模块分布：

- **CPU 2 条**：核满载（可调负载 1-100%）、核离线
- **存储 1 条**：磁盘写压
- **网络 11 条**：延迟 / 丢包 / 乱序 / 网卡 down / 降速 / 端口占用 / 服务停止 / 链路闪断 / 带宽限制 / 抖动 / TCP 丢包
- **进程 3 条**：进程退出（inject-only）/ 进程挂起 / 僵尸进程
- **NPU 16 条**：RoCE 链路 / IP / 网关 / ARP / 路由 / 策略路由 / 带宽 / MTU / DSCP / RoCE 端口 等

合并上游 8→4 add/del 对后，故障目录从初版 37 条精简为 33 条后，batch2 扩充至 58 条：删除 `rNPU_fec_change`（910B4 硬件不支持）、`rNPU_pfc_change` / `rNPU_prio_tc_change` / `rNPU_route_clear`（910C 真机验证驱动不支持），NPU 模块由 20 条减至 16 条。

#### Web 控制台（dcat serve）

- HTTP 长驻服务，浏览器远程查看 active/history/catalog + 注入/清理故障
- 默认只读（`--allow-write` 开启写操作），绑定 `127.0.0.1`（SSH 隧道访问）
- 支持 `--port` / `--bind` / `--webroot` 参数
- 安全加固：`realpath()` 路径穿越防护 + `%2e` URL 编码检测 + `--port` CLI 校验

#### 命令格式

- 子命令式 `dcat <subcommand> [uid] --key=value ...`
- 支持 `inject` / `clean` / `query` / `list` / `serve` 五个操作
- `--help` 子命令感知帮助（`dcat inject --help` 列出所有支持 inject 的故障及参数）
- `--config <path>` / `--plugins <dir>` 全局选项
- `dcat list` 文本表格输出（对齐易读）
- CPU 核心上限 127 → 1024（`DCAT_MAX_CORES` 宏 + 128 字节位图，覆盖 640 核 aarch64 服务器）

#### per-operation 参数声明

- conf 按操作分别声明必填/可选参数：`inject_required` / `inject_optional` / `clean_required` / `clean_optional` / `query_required` / `query_optional`
- 空字段可省略（默认空字符串）
- precheck 按操作检查各自的 `*_required`，错误提示带参数名
- `dcat query` / `dcat clean` 必须指定 uid + 对应参数

#### reinject 默认拒绝 + --force 原子替换

- 对已存在活动记录的同一 uid 注入（参数重叠）默认拒绝，退出码 5
- 错误消息列出重叠记录 id 与前次参数，提示 `--force`
- `--force`：先清理重叠记录再注入，clean 由 cnf 脚本提供，实现原子替换

#### NPU 真机验证（Atlas 910B4）

- 16 条 `rNPU_*` 故障全部通过真机 inject/clean/query 验证（device 2 & 5）
- 4 种清理策略（reverse op / sidecar replay / set to max / cfg recovery）真机全覆盖
- `link_down.sh` 修复：`hccn_tool -link -s down` 交互式 y/n 确认 → `echo y |` 自动应答
- NPU inject 回读：`fault_present()` 条件化，inject 参数降为可选（query 场景）
- 9 条 NPU 故障的 inject/clean/setup 参数从合成假值改为实机真实拓扑值，`user_manual.md` 第五章新增 §0 前置参数查询章节（7 步）指导用户适配
- `npu_foreach_chip`：无参 query 遍历所有 NPU device（与 CPU/网络模块行为一致）
- 9 个 `fault_present` 加 sidecar 回退：无参 query 时用 sidecar 存在性判断故障状态
- 4 个 add 类故障（arp_poison/route_add/iprule_add/iproute_add）inject 加 `sidecar_save`，clean 加 `sidecar_clear`
- query 输出加 `FAULT CONFIRMED` / `FAULT NOT ACTIVE` 文本提示
- `confirmed` 字段修复：echo 退出码恒 0 导致 dispatch.c 误判 → 加 `false` 让 NOT ACTIVE 返回 1

#### 输入校验加固

- `net_loss.sh`：loss_pct 加 0-100 范围校验
- `net_delay.sh`：delay_ms 加正整数校验
- 7 个网络脚本加 iface 白名单（防注入）、port/rate/delay/jitter 数值校验
- `disk_write_overload.sh`：symlink 防护（`readlink -f` 检查拒绝）+ clean 先 SIGTERM 再 SIGKILL 防 dd 孤儿 + workers/size_mb/device 输入校验，device 限制 `/tmp` / `/var/tmp`
- `cpu_overload.sh`：cores/load_pct 输入校验，query 按 `--cores` 参数过滤
- `rCPU_core_offline`：校验核心实际状态
- NPU 脚本 `npu_validate_chip` 防命令注入
- `proc_hang.sh`：pid=0 进程组安全（加正整数校验）
- `rPROC_zstate`：重新设计为 kill 真实进程制造僵尸（参数从 count 改为 pid）

#### Bug 修复

- `state_add` 返回值检查（满表时不写 record_id，返回错误）
- `state_save` cJSON NULL check + fputs/fclose 返回值检查 + I/O 移出锁范围
- state record_id 64-bit 防 overflow + started_at 可读格式
- `executor` clear_stale_env_params 防 clean 循环 setenv 泄漏
- `_POSIX_C_SOURCE=200809L` 确保 strict C11 可移植
- `net_delay.sh` / `net_jitter.sh`：query 正则 `[0-9]+` → `[0-9.]+` 匹配 tc 小数输出
- `ip_change.sh`：fault_present grep 子串匹配 → 精确 IP 值比较；静态 SIDECAR → 动态 `SIDECAR_FN()`
- `bw_limit.sh`：clean 用未定义变量 `$MAX_BW` → 改为 sidecar 的 `$orig`
- `net_degrade.sh`：从 ethtool 改为 tc tbf（物理+虚拟网卡均可测）
- `gw_change.sh`：无原网关时 sidecar 未保存 → 始终保存 `${orig:-none}`，sidecar 保存修复
- `disk_write_overload.sh`：trap 杀 dd 子进程防孤儿
- STATE-7 `rNPU_mtu_mismatch`：`--size=1500` 在 MTU=1500 机器上是 no-op → 改为 1280

#### E2E 测试框架（354 步骤 / 165 流程）

- 8 类混沌工程测试矩阵：FUNC / BOUND / SEC / STATE / RES / CLI / CONC / INTER
- NPU 真机适配：chip=0→2、IP 网段修正、grep 正则匹配 hccn_tool 真机输出
- sweep 加 NPU stale state 条件清理（ip_rule/route/ip_route/link up）
- `run_e2e.py` 修复：CONC `& wait` 和 SEC `; rm` 的 shell 语法在 shlex.split 下不生效
- 新增 27 条用例：NPU FUNC-Q（query<uid> confirmed）、NPU BOUND（bw_limit/size/port/dscp）、NPU RES（clean --all）、NPU STATE（reinject 拒绝）
- `test_smoke_cpu` 亲和性感知：用 `sched_getaffinity` 自动选核，修复容器 cpuset 屏蔽问题

#### 合并上游

- PR #20: state record_id 64-bit 防 overflow + started_at 可读格式
- PR #21: rCPU_core_offline 校验核心实际状态
- PR #22-26: clean --all / 无参 clean / state reconcile / e2e 测试框架

#### 构建系统

- CMake ≥ 3.10，C11（`_POSIX_C_SOURCE=200809L`），`-Wall -Wextra -Werror`
- cJSON vendored + pthread + dlopen（`${CMAKE_DL_LIBS}`）
- `make install` 全局 symlink

#### 文档

- README（含依赖说明 + 一键安装脚本 `scripts/install_deps.sh`）
- 用户手册 `User_Manual.md`（58 条故障 × 7 字段，含目录，NPU 章节含 §0 前置准备 + 实机示例）
- 手动测试指南 `docs/Manual_Test_Reference.md`
- SPEC（技术规格）+ DESIGN（架构设计）
- Release Notes + docs/Test_Report.md

### 测试

- **ctest**：24/24 全通过
  - Tier 0 核心单元测试（types / output / config / registry / executor / precheck / state / injectors / dispatch / reinject / cli / faults / help / plugin_manager + plugin_integration）
  - Tier 1 mock 表驱动故障测试（58 条全覆盖）
  - Tier 2 脚本语法检查（sh -n 全部 33 脚本 + _common.sh）
  - Tier 3 真实执行测试（非 root 故障）
  - root 冒烟测试（smoke_root.sh）
- **E2E**：354 步骤 / 165 流程，8 类混沌工程测试矩阵

### 已知限制

- NPU 16 条故障需 Atlas 物理机 + `hccn_tool` 真机验证（910B4 + 910C 已验证）
- 网络 11 条 / 进程 3 条故障依赖 root 权限（`tc` / `iptables` / `ip` / `ethtool` / `systemctl`）
- `rNET_degrade` 在 dummy 虚拟网卡上不支持（需真实物理网卡）
- 不实现超时自动恢复：所有可恢复故障注入后需用户手动 clean
- 不实现安全确认交互：预检仅做静态校验
- 编译注入器 `builtin_injectors[]` 为空：所有故障均走 cnf + 脚本路径

---

*本文档仅追加新版本记录，不删除历史。*
