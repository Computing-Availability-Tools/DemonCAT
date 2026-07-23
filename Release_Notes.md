# DemonCAT Release Notes

> 本文档按时间倒序记录每次发布的版本信息。每次发布在顶部追加，不删除历史记录。

---

## v0.1.1

| 项目 | 说明 |
|------|------|
| 版本号 | v0.1.1 |
| 发布时间 | 2026-07-23 |
| 发布人 | SamWongCc |
| 平台支持 | Linux (x86_64), WSL 兼容 |
| 合并来源 | fork/main (90afb2a) |

### 变更摘要

**P0 关键修复：**
- **[CRITICAL] state_add 返回值检查**（`dispatch.c`）：状态表满(32条)时 `state_add` 返回 0，原代码仍返回 `status:ok` + `record_id:0` → 故障已执行但不可清理。现检查返回值并返回 error，提示用户先清理已有注入
- **README 故障表修正**：从 18 条补全到 39 条（补齐 20 条 NPU + `rCPU_overload_yes`），README 描述补 NPU 模块
- **断链修复**：`smoke_test_manual.md` → `manual_test_guide.md`（Release_Notes / test_report / smoke_root.sh 三处）

**P1 健壮性修复：**
- **executor 缓冲区满 busy-loop**（`executor.c`）：`read` 循环在缓冲区满后不清零 `total`，导致逐字节 busy-loop 读取丢弃 + CPU 烧。改为 `break` 停止读取
- **setenv 参数泄漏**（`executor.c` + `dispatch.c`）：clean 循环遍历多条记录时前一条的 `DCAT_PARAM_*` 泄漏到后一条。新增 `executor_clear_env_params()` 在每条记录前清除声明的环境变量
- **NPU 脚本命令注入防护**（`_common.sh` + 20 个 NPU 脚本）：新增 `npu_validate_chip()` 校验 chip 为 0-9 单数字，防止 `$HCCN` 未加引号导致命令注入
- **state_save NULL deref + 持锁 I/O**（`state.c`）：`cJSON_PrintUnformatted` 返回值未检查 → OOM 时 `fputs(NULL)` segfault；`fputs`/`fclose` 返回值未检查 → 磁盘满时 state 文件被静默替换为空。现检查所有返回值，并将文件 I/O 移出锁范围
- **strict C11 可移植性**（`CMakeLists.txt`）：添加 `_POSIX_C_SOURCE=200809L` 编译定义，`CMAKE_C_EXTENSIONS=OFF` 时可正常编译（原需 gnu11 扩展）
- **stderr 详情转发**（`dispatch.c`）：脚本失败时原代码返回 generic "script failed"，丢失 executor 捕获的 stderr 片段。新增 `extract_err_msg()` 从 result_t 中提取真实错误消息
- **main.c exit code 不一致**（`main.c`）：dispatch 返回 NULL(OOM) 时 exit code=0，与 JSON 输出的 error 不一致。改为 `DCAT_E_RUN`(1)

**文档同步：**
- **DESIGN.md §3.4**：executor_run 签名更新（删除 `timeout_ms`/`timer_create`），新增 `executor_clear_env_params` + `executor_set_env` 声明
- **DESIGN.md §4.3**：CPU 模块 2→3 条（补 `rCPU_overload_yes`）
- **DESIGN.md §8 + SPEC.md**：脚本路径 `config/scripts/` → `src/scripts/`，38 条 → 39 条，v0.1 状态 "待开发" → "已完成"
- **SPEC.md §3.3**：补 `rCPU_overload_yes` 行 + `rDISK_write_overload` optional_params 补 `size_mb(默认200)`

### 已知限制

- NPU 20 条故障需 Atlas 物理机 + `hccn_tool` 真机验证；WSL / 普通 Linux / CI 无 NPU 设备无法模拟（自动化仅覆盖下发命令串）
- 网络 11 条故障依赖 root 权限与 `tc` / `iptables` / `ip` / `ethtool` / `systemctl` 工具链（仅 `rNET_port_occupy` 为纯用户态，已纳入 Tier 3 自动测试）
- 不实现超时自动恢复：所有可恢复故障注入后需用户手动 `clean`（YAGNI，本期聚焦同步执行与状态跟踪）
- 不实现安全确认交互：预检仅做静态校验（uid 存在 / op 支持 / 必填参数 / 脚本可执行），不提供注入前的二次确认提示
- 编译注入器 `builtin_injectors[]` 为空：所有故障均走 cnf + 脚本路径，进程内自定义逻辑注入器仅头文件 `src/injectors/injector.h` 留位（YAGNI）

---

## v0.1.0

| 项目 | 说明 |
|------|------|
| 版本号 | v0.1.0 |
| 发布时间 | 2026-07-23 |
| 发布人 | SamWongCc |
| 平台支持 | Linux (x86_64), WSL 兼容 |

### 变更摘要

- **核心框架**：9 个 C 模块构成稳定二进制核心——`cli`（命令解析）/ `config`（INI 配置加载）/ `registry`（故障目录查表）/ `executor`（fork/exec 执行）/ `precheck`（注入前预检护栏）/ `state`（注入记录与持久化）/ `dispatch`（命令分发）/ `output`（JSON 输出边界）/ `main`（入口）；另含编译注入器 stub（`src/injectors/`）作为高级扩展点留位
- **命令格式**：子命令式 `dcat <subcommand> [uid] --key=value ...`，支持 `inject` / `clean` / `query` / `list` 四个操作；所有参数以 `--key=value` 标志传入，全局选项 `--config` / `--help` 与参数标志混合使用
- **参数校验分层**：dcat 负责**结构校验**（`required_params` 是否齐全且非空、未声明参数直接拒绝、脚本可执行性）；参数值合法性（核号范围、iface 是否存在、chip 是否有效）由脚本自行**语义校验**，职责边界清晰
- **同步阻塞执行**：所有 inject / clean / query 均通过 `fork/exec + waitpid` **同步阻塞**调用脚本，执行完返回；需要长驻的故障（CPU 过载、端口占用、僵尸生成、磁盘写压、链路闪断、D 状态进程）由脚本自行 spawn 子进程并写 pidfile / sidecar，clean 时重跑脚本读取清理
- **故障目录**：39 条故障（CPU 3 + 存储 1 + 网络 11 + 进程 4 + NPU 20），全部以**外部脚本 + 声明式配置**（`demoncat.conf`）接入；加一个故障 = 加一个脚本 + 配置文件一段，**免重新编译**（开闭原则）
- **测试体系**：分层 5 级——Tier 0 核心单元测试（6：cli / registry / precheck / state / output / executor-mock）+ Tier 1 mock 表驱动故障测试（39 条全覆盖下发命令串与环境变量）+ Tier 2 脚本语法检查（`sh -n`，39 脚本）+ Tier 3 真实执行测试（无需 root 的 7 条故障：rCPU_overload / rCPU_overload_yes / rDISK_write_overload / rNET_port_occupy / rPROC_exit / rPROC_hang / rPROC_zstate）+ 手动冒烟测试（32 条需 root 或 NPU 硬件）
- **文档**：README + 用户手册（`docs/user_manual.md`，39 条故障 × 7 字段）+ 手动测试指南（`docs/manual_test_guide.md`，覆盖 10 条高危故障）+ SPEC（技术规格）+ DESIGN（架构设计）
- **构建**：CMake ≥ 3.10，C11（ISO/IEC 9899:2011），`-Wall -Wextra -Werror`，pthread 状态锁，cJSON 单文件库 vendored 进仓库（`third_party/cjson/`），输出格式 JSON

### 已知限制

- NPU 20 条故障需 Atlas 物理机 + `hccn_tool` 真机验证；WSL / 普通 Linux / CI 无 NPU 设备无法模拟（自动化仅覆盖下发命令串）
- 网络 11 条故障依赖 root 权限与 `tc` / `iptables` / `ip` / `ethtool` / `systemctl` 工具链（仅 `rNET_port_occupy` 为纯用户态，已纳入 Tier 3 自动测试）
- 不实现超时自动恢复：所有可恢复故障注入后需用户手动 `clean`（YAGNI，本期聚焦同步执行与状态跟踪）
- 不实现安全确认交互：预检仅做静态校验（uid 存在 / op 支持 / 必填参数 / 脚本可执行），不提供注入前的二次确认提示
- 编译注入器 `builtin_injectors[]` 为空：所有故障均走 cnf + 脚本路径，进程内自定义逻辑注入器仅头文件 `src/injectors/injector.h` 留位（YAGNI）
