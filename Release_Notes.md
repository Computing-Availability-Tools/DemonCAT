# DemonCAT Release Notes

> 本文档按时间倒序记录每次发布的版本信息。每次发布在顶部追加，不删除历史记录。

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
- **文档**：README + MANUAL（用户手册）+ 冒烟测试手册（`docs/smoke_test_manual.md`，覆盖 32 条无法自动测试的故障）+ SPEC（技术规格）+ DESIGN（架构设计）
- **构建**：CMake ≥ 3.10，C11（ISO/IEC 9899:2011），`-Wall -Wextra -Werror`，pthread 状态锁，cJSON 单文件库 vendored 进仓库（`third_party/cjson/`），输出格式 JSON

### 已知限制

- NPU 20 条故障需 Atlas 物理机 + `hccn_tool` 真机验证；WSL / 普通 Linux / CI 无 NPU 设备无法模拟（自动化仅覆盖下发命令串）
- 网络 11 条故障依赖 root 权限与 `tc` / `iptables` / `ip` / `ethtool` / `systemctl` 工具链（仅 `rNET_port_occupy` 为纯用户态，已纳入 Tier 3 自动测试）
- 不实现超时自动恢复：所有可恢复故障注入后需用户手动 `clean`（YAGNI，本期聚焦同步执行与状态跟踪）
- 不实现安全确认交互：预检仅做静态校验（uid 存在 / op 支持 / 必填参数 / 脚本可执行），不提供注入前的二次确认提示
- 编译注入器 `builtin_injectors[]` 为空：所有故障均走 cnf + 脚本路径，进程内自定义逻辑注入器仅头文件 `src/injectors/injector.h` 留位（YAGNI）
