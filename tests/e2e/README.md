# DemonCAT E2E 测试

CSV 驱动的 `dcat` 二进制端到端测试框架：**混沌工程 8 类测试矩阵** → 自动生成用例 → 串行执行 → linux 命令观测断言 → 输出报告。

## 文件

| 文件 | 说明 |
| --- | --- |
| `gen_cases.py` | 从故障目录 + 观测/边界/安全知识自动生成 `cases.csv`（468 步骤 / 249 流程） |
| `cases.csv` | 用例表（468 步骤 / 249 流程，8 类分类） |
| `run_e2e.py` | 执行框架（读 cases.csv → 串行执行 → 写 results/report） |
| `results_<时间戳>.csv` | 每次运行产物（含实际结果列，已 gitignore） |
| `report.md` | 每次运行的逐用例报告（已 gitignore） |

## 8 类混沌工程测试矩阵

| 分类 | 前缀 | 用例数(步骤) | 流程数 | 覆盖内容 | 混沌工程维度 |
| --- | --- | --- | --- | --- | --- |
| **FUNC** | `FUNC-` | 196 | 64 | 故障 inject→verify→clean→query 全链路 + query\<uid\> confirmed + 插件 | 功能基线 |
| **BOUND** | `BOUND-` | 98 | 95 | 每参数类型系统性覆盖（整数越界/空值/格式错误/枚举非法），含 NPU bw_limit/size/port/dscp | 边界值 |
| **SEC** | `SEC-` | 59 | 46 | 命令注入(inject+clean+query) + 权限边界 + 主机安全 + symlink 攻击 | 安全 |
| **STATE** | `STATE-` | 37 | 10 | clean×2/--force/reinject 拒绝/query 幂等/并发 inject 同/不同资源 | 状态一致性 |
| **RES** | `RES-` | 27 | 7 | state 丢失/损坏/孤儿/幽灵/clean --all 幂等/state 表满 | 韧性/自愈 |
| **CLI** | `CLI-` | 28 | 21 | 解析错误 + 帮助 + 退出码 + --config + 未知 uid + serve | CLI 接口 |
| **CONC** | `CONC-` | 9 | 3 | 同时 inject+clean / 双进程写 state / clean --all + inject | **并发竞争** |
| **INTER** | `INTER-` | 14 | 3 | 多故障叠加 / clean 一个不影响其他 / clean --all 后逐 verify | **故障交互** |
| **总计** | | **468** | **249** | | |

### 分类演进（v1 → v2）

| 旧分类（14 类） | 新分类（8 类） | 说明 |
| --- | --- | --- |
| F + Q + PLG | **FUNC** | 功能基线合并：故障全链路 + query\<uid\> confirmed + 插件 |
| B | **BOUND** | 边界值扩展：从 17 条→99 条系统性覆盖 |
| I + P + H | **SEC** | 安全合并：命令注入(inject+clean+query) + 权限边界 + symlink 攻击 |
| S | **STATE** | 状态一致性：clean×2/--force/reinject/并发 inject |
| R + CHAOS | **RES** | 韧性合并：state 丢失/损坏/孤儿/幽灵/state 表满 |
| CLI + SUBHELP + MISC + CFG | **CLI** | CLI 接口合并：解析错误 + 帮助 + 退出码 + --config |
| — | **CONC** | **新增**：并发竞争（同时 inject+clean / 双进程写 state） |
| — | **INTER** | **新增**：故障交互（多故障叠加 / clean 一个不影响其他） |

## 用法

```bash
# 1. 生成/更新用例（改 config/demoncat.conf 或观测映射后重跑）
python3 tests/e2e/gen_cases.py

# 2. 执行（生产全量跑，不 skip）
python3 tests/e2e/run_e2e.py                 # 当前用户权限跑
sudo python3 tests/e2e/run_e2e.py           # root：跑全含 tc/iptables/sysfs 等

# 只跑指定分类（逗号分隔前缀）
python3 tests/e2e/run_e2e.py --flows FUNC,BOUND,SEC

# 只跑指定 flow
python3 tests/e2e/run_e2e.py --flows FUNC-rCPU_overload,BOUND-1,SEC-I1

# 不回填 test_report.md（仅出 results/report）
python3 tests/e2e/run_e2e.py --no-append
```

`run_e2e.py` 选项：`--cases <path>` `--dcat <path>` `--out-dir <dir>` `--report <test_report.md>` `--no-append` `--flows <前缀>`

## 执行模型

- **严格串行**：按 flow_id 顺序，单线程，一 flow 跑完（含环境恢复）才跑下一个。
- **每例前后幂等清扫**（dcat 命名空间内，对宿主安全）：清 dcat 残留进程（`pkill -x perl/dd/yes` 等）、`/tmp/dcat-*` 工件、隔离 state、测试网卡 qdisc/链路重置、CPU 按标记恢复 online。清扫脚本经临时文件执行，避免 `pkill -f 'PATTERN'` 匹配 sweep 自身。
- **状态隔离**：`HOME=/tmp/dcat_e2e_home`（dcat 经 `~/.demoncat/state.json` 展开，不用无效的 `DCAT_STATE_FILE`）。
- **provision**：`sleep_pid`（经 watcher 派生，避免 rPROC_zstate clean 杀父=框架自杀）、`free_port`、`dummy_iface`(dcat-e2e0)、`real_phy`、`noncritical_svc`。资源不就绪则用例自然 FAIL。
- **失败安全网**：任一步异常仍跑后置清扫；框架退出注册 atexit 总清扫。

## 不 skip / 全量跑

生产要求全量跑，**不 skip**：root/NPU/硬件依赖用例在缺资源环境会 **FAIL**（非 SKIP），生产（NPU 硬件 + root + sysfs 可写 + cron 运行）应 **0 FAIL 全绿**。

- **FUNC 中 NPU 20 条**：无 `hccn_tool` → FAIL（需 Atlas NPU 物理机）。
- **FUNC-rCPU_core_offline**：默认实跑，瞬态下线真实核 `cpu1`（clean+清扫恢复）。
- **SEC-P 类**：root 框架下用 `runuser -u nobody` 降权验证非 root 被拒绝。
- **SEC-H 写入边界**：`device=/tmp` 安全路径（不污染 `/etc`）。
- **CONC/INTER**：并发竞争与故障交互，验证 dcat 在多进程/多故障场景下的一致性。

## 产物

- `results_<时间戳>.csv`：每步 `actual_exit_code/actual_json/verify_actual/result/error_code/duration/timestamp`，含 `expected_behavior` 列（测试目的）。
- `report.md`：汇总 + **测试用例明细（含测试目的）** + 8 类分类统计(含分类说明) + 失败用例 + 跳过原因。
- `test_report.md` §10：追加 e2e 汇总段，含逐用例测试目的表（`--no-append` 可关）。
- CI artifact（`e2e-x86` / `e2e-arm64`）仅含本次运行相关产物：`report.md` + `results_*.csv` + `e2e_run.log` + `failures_*.log`，不含全量用例库 `cases.csv`。

## 断言 DSL（verify_assert 列）

| 断言 | 说明 | 作用对象 |
| --- | --- | --- |
| `>=N <=N ==N !=N` | 数值比较 | verify_out |
| `eq:STR ne:STR` | 字符串相等/不等 | verify_out |
| `contains:STR notcontains:STR` | 包含/不包含 | verify_out |
| `out_contains:STR` | 命令 stdout 包含 | cmd_json（命令自身输出） |
| `regex:PAT` | 正则匹配 | verify_out |
| `empty nonempty` | 空/非空 | verify_out |
| `exists:PATH notexists:PATH` | 文件存在/不存在 | 文件系统 |
| `exitcode:N` | 退出码等于 | cmd_rc |
| `state_empty` | state 数据为空 | cmd_json |
| `state_contains:uid` | state 包含 uid | cmd_json |
| `state_not_contains:uid` | state 不包含 uid | cmd_json |

## 示例：新增故障后的测试更新

```bash
# 1. 在 config/demoncat.conf 添加新故障
[fault.rNEW_test]
module = test
script = src/scripts/test/new_test.sh
supported_ops = inject,clean,query
inject_required = param1

# 2. 在 gen_cases.py 的 OBS 字典添加观测知识
"rNEW_test": dict(module="test", inject_args="--param1=value1",
    clean_args="--param1=value1", provision="none", precondition="none",
    v_cmd="some_verify_cmd", v_assert=">=1",
    c_cmd="some_verify_cmd", c_assert="==0"),

# 3. 重新生成用例
python3 tests/e2e/gen_cases.py

# 4. 执行测试
sudo python3 tests/e2e/run_e2e.py
```

## 参考

- 测试设计：[docs/DESIGN.md §10](../../docs/DESIGN.md)
- 技术规格：[SPEC.md §9](../../SPEC.md)
- 项目主页：[README.md](../../README.md)
