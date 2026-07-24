# DemonCAT 端到端测试报告

> **日期：** 2026-07-23
> **测试人：** 资深软件测试
> **被测对象：** dcat 二进制（develop 分支，commit feat(catalog): expand demoncat.conf to 38 faults）
> **测试环境：** WSL Ubuntu 24.04 / gcc 13.3.0 / cmake 3.28.3 / Linux 5.x

## 1. 测试范围

1. **单元测试**：13 个 test_*.c（ctest 驱动）
2. **端到端命令行验证**：38 条声明故障的 inject/clean（含 inject-only 拒绝）+ list + 插件
3. **威胁操作处理**：当前所有脚本为占位 echo（无实际故障效果），不构成关机/断网等威胁，可安全全量验证

## 2. 单元测试结果（ctest）

```
100% tests passed, 0 tests failed out of 13
Total Test time = 0.09 sec
```

| # | 测试 | 覆盖 | 结果 |
|---|---|---|---|
| 1 | test_types | params 辅助 + dcat_key_to_env + subset match | PASS |
| 2 | test_output | result_ok/err JSON schema（inject-only 无 record_id） | PASS |
| 3 | test_config | INI 解析 38 条 + resolve_script + derive_project_root | PASS |
| 4 | test_registry | fault_def 表 find/list/count | PASS |
| 5 | test_executor_mock | mock 捕获 cmd+env + build_env + run_raw | PASS |
| 6 | test_precheck | 4 步预检 + 未声明参数拒绝 + inject-only | PASS |
| 7 | test_state | add/find_by_params/list/inactive + 持久化 roundtrip | PASS |
| 8 | test_injectors | 空注册表 find 不命中 | PASS |
| 9 | test_dispatch | inject/clean/list/query 三级路由 + state | PASS |
| 10 | test_cli | argv 子命令 + --key=value + 全局选项排除 | PASS |
| 11 | test_faults | 表驱动 3 条示例故障 mock | PASS |
| 12 | test_plugin_manager | find 未命中 + 空目录加载 0 | PASS |
| 13 | test_plugin_integration | dlopen + 三级回退 + inject/clean + state | PASS |

## 3. 端到端命令行验证（38 条故障 inject+clean）

### 验证方法
对每条故障执行 `dcat inject <uid> --<required>=<val>` → `dcat clean <uid> [--<k>=<v>]`，断言 JSON `status:ok` + 退出码。

### 结果汇总

```
PASS=38  FAIL=0  TOTAL=38
```

### 按模块明细

| 模块 | 故障数 | inject | clean | 备注 |
|---|---|---|---|---|
| cpu | 2 | 2 PASS | 2 PASS | rCPU_overload / rCPU_core_offline |
| network | 11 | 11 PASS | 11 PASS | rNET_delay/loss/reorder/down/degrade/port_occupy/service_stop/link_flap/bw_limit/jitter/tcp_loss |
| process | 4 | 4 PASS | 3 PASS + 1 拒绝 | rPROC_exit inject-only（clean exit 3 拒绝） |
| storage | 1 | 1 PASS | 1 PASS | rDISK_write_overload |
| npu | 20 | 20 PASS | 20 PASS | rNPU_link_down…roce_port_change |

### 关键验证点

- **可恢复故障（37 条）**：inject 返回 `status:ok` + `record_id` → clean 返回 `status:ok` → state 持久化闭环（跨进程 inject→clean 生效）
- **inject-only（rPROC_exit）**：inject 返回 `status:ok` 无 `record_id` → clean 返回 `error code:3 op not in supported_ops`，exit=3
- **三级回退**：rSAMPLE_test 动态插件不在 cnf/injector，plugin_find 命中，inject+clean+query 通过
- **list 输出**：39 条（38 cnf + 1 动态插件 rSAMPLE_test）

## 4. 之前轮次端到端用例（15 用例，仍有效）

| 用例 | 验证点 | 结果 |
|---|---|---|
| list | 3 cnf + 1 插件 | PASS |
| rCPU_overload inject→query(state)→clean→query(空) | state 持久化闭环 | PASS |
| rNET_delay inject→query(uid,脚本直通+confirmed)→clean | query 有 uid 走 system 直通 | PASS |
| rPROC_exit inject→clean 拒绝 | inject-only 语义 | PASS |
| rSAMPLE_test 插件 inject→query→clean→list 含插件 | 动态插件三级回退 | PASS |
| rNOPE unknown uid | exit 4 | PASS |

## 5. 威胁操作说明

当前 38 个故障脚本均为**占位 echo**（`echo "<name>: ${DCAT_OP} (placeholder)"`），不执行真实故障操作（无 kill/tc/ip/iptables/hccn_tool 调用），**不构成关机、断网、进程杀死等威胁**，故全量安全验证。

后续按模块计划实现真实脚本时，对真正危险的故障（rPROC_exit kill -9、rNET_down 断网、rNPU_link_down 等）需在受控环境单独验证。

## 6. 结论

**全部测试通过**：
- 单元测试 13/13 全绿
- 端到端 38/38 故障 inject+clean 符合预期（含 inject-only 拒绝）
- 动态插件三层扩展架构（cnf → 编译注入器 → dlopen 插件）验证生效
- state 持久化闭环正确
- list 输出 39 条（38 cnf + 1 插件）

dcat 核心框架 + 动态插件架构 + 38 条故障目录实现质量符合预期，可进入下一阶段（真实故障脚本填充）。
