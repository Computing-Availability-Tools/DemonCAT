# DemonCAT 测试报告

> **项目**: DemonCAT (dcat) — Linux 计算故障注入工具
> **版本**: v0.1
> **日期**: 2026-07-25
> **测试执行**: CTest 自动化 + 手动验证

---

## 1. 测试概述

### 1.1 测试目标

验证 DemonCAT v0.1 核心框架 + 37 条故障的完整性和正确性：

- 核心框架 9 模块 + 插件架构功能正确
- 全部 37 条故障的 inject/clean/query 下发路径正确（mock 表驱动）
- 全部 37 个脚本无语法错误
- 6 条无需 root 的故障端到端可执行（真实脚本执行）
- root 级冒烟测试覆盖 10 条可测故障
- strict C11 (`CMAKE_C_EXTENSIONS=OFF`) 可移植性验证
- --help 子命令帮助系统验证

### 1.2 测试结果汇总

| 指标 | 结果 |
|------|------|
| 测试总数 | **22** |
| 通过 | **22** |
| 失败 | **0** |
| 跳过 | **0** |
| 通过率 | **100%** |
| `cmake --build` | ✅ 通过（-Wall -Wextra -Werror, 0 warnings） |
| 故障目录总数 | 37 (CPU 2 + 存储 1 + 网络 11 + 进程 3 + NPU 20) |
| root 冒烟 | 10 PASS / 0 FAIL / 3 SKIP |

---

## 2. 测试环境

| 项目 | 配置 |
|------|------|
| 操作系统 | Linux (WSL2, x86_64) |
| 编译器 | gcc 13.3.0 |
| 构建系统 | CMake 3.28.3 |
| C 标准 | C11 (gnu11) + strict C11 (`_POSIX_C_SOURCE=200809L`) |
| 第三方依赖 | cJSON v1.7.18 (vendored) |
| 线程库 | pthread |
| 测试框架 | CTest |
| NPU/hccn_tool | 无（mock 驱动） |
| root 权限 | 部分测试使用 sudo |

---

## 3. 编译与静态检查

| 检查项 | 命令 | 结果 |
|--------|------|:----:|
| 构建 | `cmake -B build && cmake --build build` | ✅ |
| 编译选项 | `-Wall -Wextra -Werror` | ✅ 零警告 |
| strict C11 | `cmake -DCMAKE_C_EXTENSIONS=OFF -B build_strict` | ✅ |
| 二进制 | `build/dcat` | ✅ 生成成功 |
| 动态插件 | `plugins/libsample.so` | ✅ 生成成功 |

---

## 4. 各层测试结果

### 4.1 Tier 0: 核心单元测试 (13 个)

| 测试 | 覆盖范围 | 结果 | 耗时 |
|------|---|:----:|:----:|
| test_types | params_t helpers (init/set/find/match/env) | PASS | 0.00s |
| test_output | result_ok/err/print/free + JSON schema | PASS | 0.00s |
| test_config | INI 解析 + fault_def 载入 + resolve_script + derive_project_root | PASS | 0.00s |
| test_registry | fault_count=37 + fault_def 查找 + list | PASS | 0.01s |
| test_executor | mock 拦截 + build_env + apply_env + check_tool | PASS | 0.00s |
| test_precheck | per-op required 校验 + undeclared param 拒绝 | PASS | 0.01s |
| test_state | params 存储 + find_by_params + 持久化 + 并发注入 | PASS | 0.00s |
| test_injectors | injector_t 接口 + injector_find (空数组) | PASS | 0.00s |
| test_dispatch | dispatch_route 3-tier 路由 + clean/query 拒绝空参数 | PASS | 0.01s |
| test_cli | 子命令解析 + --config/--plugins/--help 全局选项 | PASS | 0.00s |
| test_faults | 表驱动 inject/clean/query (3 条示例故障) | PASS | 0.01s |
| test_help | --help 全局/子命令/uid 详情 + 故障列表 | PASS | 0.01s |
| test_plugin_manager | dlopen 加载 + ABI 版本检查 + plugin_find | PASS | 0.00s |

### 4.2 插件集成测试

| 测试 | 覆盖范围 | 结果 | 耗时 |
|------|---|:----:|:----:|
| test_plugin_integration | libsample.so 加载 + inject/clean dispatch + state | PASS | 0.01s |

### 4.3 Tier 1: Mock 表驱动故障测试 (37 条全覆盖)

| 测试 | 覆盖故障数 | 覆盖模块 | 结果 | 耗时 |
|------|:---:|---|:----:|:----:|
| test_faults_cpu_storage | 3 | CPU(2) + 存储(1) | PASS | 0.01s |
| test_faults_network | 11 | 网络(11) | PASS | 0.03s |
| test_faults_process | 3 | 进程(3) | PASS | 0.01s |
| test_faults_npu | 20 | NPU(20) | PASS | 0.04s |

> 每条故障验证：inject 下发正确的脚本路径 + DCAT_OP/DCAT_UID/DCAT_PARAM_* 环境变量 + 退出码 + record_id（可恢复故障）；clean 传存储参数 + DCAT_OP=clean；inject-only 故障验证 clean/query 被拒绝（退出码 3）。

### 4.4 Tier 2: 脚本语法检查

| 测试 | 检查范围 | 结果 | 耗时 |
|------|---|:----:|:----:|
| test_syntax | 全部 37 个 .sh 脚本 + _common.sh (`sh -n`) | PASS | 0.14s |

### 4.5 Tier 3: 真实执行测试 (6 条无需 root 的故障)

| 测试 | 故障 | 验证内容 | 结果 | 耗时 |
|------|---|---|:----:|:----:|
| test_smoke_cpu | rCPU_overload (50%+100%) | inject→pgrep perl≥1→clean→pgrep=0 | PASS | 2.03s |
| test_smoke_process | rPROC_hang | inject→SIGSTOP(T)→clean→SIGCONT→恢复 | PASS | 6.45s |
| | rPROC_zstate | inject→kill→zombie(Z)→clean→kill parent→reaped | PASS | |
| | rPROC_exit | inject-only→clean/query 拒绝(退出码 3) | PASS | |
| test_smoke_storage | rDISK_write_overload | inject→pgrep dd≥2→clean→pgrep=0 | PASS | 5.05s |
| | rNET_port_occupy | inject→ss 端口被占→clean→端口释放 | PASS | |

### 4.6 root 级冒烟测试

详见 `tests/smoke_root.sh`。按原因分类：

| 结果 | 条数 | 故障列表 |
|------|:---:|---|
| PASS | 10 | rCPU_core_offline, rNET_delay, rNET_loss, rNET_reorder, rNET_bw_limit, rNET_jitter, rNET_down, rNET_link_flap, rNET_tcp_loss, rPROC_zstate |
| SKIP | 3 | rNET_degrade (dummy 网卡不支持 ethtool), rNET_service_stop (无 systemd), NPU 20 条 (无 hccn_tool) |

---

## 5. 手动验证结果

以下故障通过二进制 `./build/dcat` 逐条手动验证（inject→query→clean→query）：

| 故障 | inject | query | clean | query 后 clean |
|---|:---:|:---:|:---:|:---:|
| rCPU_overload 50% | ✅ | ✅ burn_processes:2 | ✅ | ✅ 0 进程, confirmed:false |
| rCPU_overload 100% | ✅ | ✅ 99% CPU | ✅ | ✅ confirmed:false |
| rDISK_write_overload | ✅ | ✅ FAULT CONFIRMED | ✅ | ✅ FAULT NOT ACTIVE |
| rNET_port_occupy | ✅ | ✅ confirmed:true | ✅ | ✅ confirmed:false |
| rPROC_zstate | ✅ | ✅ state=Z | ✅ | ✅ not found |
| rPROC_hang | ✅ SIGSTOP | — | ✅ SIGCONT | — |
| rPROC_exit | — | ✅ 拒绝 | ✅ 拒绝 | — |
| rNET_delay | ✅ | ✅ netem | ✅ | ✅ noqueue |
| rNET_loss | ✅ | ✅ loss 5% | ✅ | ✅ confirmed:false |
| rNET_reorder | ✅ | ✅ | ✅ | ✅ |
| rNET_down | ✅ | ✅ | ✅ | ✅ |
| rNET_bw_limit | ✅ | ✅ tbf | ✅ | ✅ |
| rNET_jitter | ✅ | ✅ | ✅ | ✅ |
| rNET_tcp_loss | ✅ | ✅ iptables | ✅ | ✅ |
| rNET_link_flap | ✅ | ✅ | ✅ | ✅ |

---

## 6. 错误提示验证

| 场景 | 命令 | 错误消息 |
|---|---|---|
| 参数名打错 | `dcat inject rCPU_overload --core=4` | `unknown parameter 'core' (not declared for rCPU_overload)` |
| UID 打错 | `dcat inject rCPU_overloa --cores=4` | `uid 'rCPU_overloa' not found in catalog (use 'dcat list' to see available faults)` |
| 子命令打错 | `dcat injec rCPU_overload` | `unknown subcommand 'injec' (available: inject, clean, query, list)` |
| 缺少必填参数 | `dcat inject rCPU_overload` | `missing required parameter 'cores' for inject` |
| query 不带 uid | `dcat query` | `uid required (use 'dcat list' to see available faults)` |
| clean 缺参数 | `dcat clean rNET_loss` | `missing required parameter 'iface' for clean` |
| cores 格式错误 | `dcat inject rCPU_overload --cores=0/1` | `invalid cores spec '0/1': use comma (0,2,4) or range (0-3)` |
| load_pct 超范围 | `dcat inject rCPU_overload --cores=0 --load_pct=500` | `load_pct must be 1-100, got: 500` |
| inject-only 拒绝 clean | `dcat clean rPROC_exit` | `op not in supported_ops` |

---

## 7. 测试文件清单

| 文件 | 层级 | 职责 |
|------|---|---|
| tests/test.h | 共享 | 测试框架宏 (RUN_TEST / ASSERT_*) |
| tests/test_faults_common.h | 共享 | mock 设置 + 断言宏 + env 检查 |
| tests/test_types.c | Tier 0 | params helpers |
| tests/test_output.c | Tier 0 | output 模块 |
| tests/test_config.c | Tier 0 | config 模块 (fault_count=37) |
| tests/test_registry.c | Tier 0 | registry + config 模块 |
| tests/test_executor_mock.c | Tier 0 | executor 模块 (mock + 真实) |
| tests/test_precheck.c | Tier 0 | precheck 模块 (per-op required) |
| tests/test_state.c | Tier 0 | state 模块 (params + persistence) |
| tests/test_injectors.c | Tier 0 | injector 接口 |
| tests/test_dispatch.c | Tier 0 | dispatch_route 3-tier + query/clean 拒绝空参数 |
| tests/test_cli.c | Tier 0 | cli 模块 |
| tests/test_faults.c | Tier 0 | 表驱动 (3 条示例) |
| tests/test_help.c | Tier 0 | --help 系统 |
| tests/test_plugin_manager.c | Tier 0 | dlopen 插件管理 |
| tests/test_plugin_integration.c | Tier 0 | 插件集成 |
| tests/test_faults_cpu_storage.c | Tier 1 | 3 条 CPU+存储 mock 测试 |
| tests/test_faults_network.c | Tier 1 | 11 条网络 mock 测试 |
| tests/test_faults_process.c | Tier 1 | 3 条进程 mock 测试 |
| tests/test_faults_npu.c | Tier 1 | 20 条 NPU mock 测试 |
| tests/check_syntax.sh | Tier 2 | 全脚本语法检查 |
| tests/test_smoke_cpu.c | Tier 3 | CPU 过载真实执行 (1 条) |
| tests/test_smoke_process.c | Tier 3 | 进程故障真实执行 (3 条) |
| tests/test_smoke_storage.c | Tier 3 | 存储+端口真实执行 (2 条) |
| tests/smoke_root.sh | root | root 级自动化测试 |

---

## 8. 结论

DemonCAT v0.1 全部 **22** 个 CTest 测试通过，零失败。root 冒烟 10 PASS / 0 FAIL / 3 SKIP。

测试覆盖：
- **核心框架**: 13 个单元测试覆盖全部模块 + 插件
- **故障目录**: 37 条故障全覆盖 (mock 表驱动 + 语法检查)
- **端到端**: 6 条非 root 故障 + 10 条 root 故障真实 inject→query→clean 验证
- **错误提示**: 9 种错误场景验证，消息具体到参数名/uid/子命令
- **可移植性**: strict C11 编译通过

**测试结论：全部通过，v0.1 可用。**

---

*测试执行时间: 2026-07-25*
*测试执行人: Automated (CTest) + Manual*
*总耗时: 13.85 秒 (CTest) + 手动验证*
