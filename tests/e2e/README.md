# DemonCAT E2E 测试

pytest + `testcases.xlsx` 驱动的 `dcat` 二进制端到端测试框架：**633 条用例**（其中 618 条含 `dcat` 命令步骤）→ 参数化执行 → 观测命令验证 + 断言 DSL → 输出报告。

## 文件

| 文件 | 说明 |
| --- | --- |
| `testcases.xlsx` | 用例源（633 条，11 列，见下），已 gitignore |
| `e2e_loader.py` | 读取 `testcases.xlsx`，逐行解析为 Case（提取 dcat 命令 / setup / 断言 / marker），`parametrized_cases()` 产出 pytest.param |
| `test_e2e_cases.py` | 参数化执行框架（每个 xlsx 用例 = 一个 pytest item，id=`TC-xxx_模块`）：skip → 前置 → provision → setup → 按序跑命令 → verify → 断言 |
| `e2e_assert.py` | 断言 DSL 计算器（复用 `apply_assert` 基础算子 + `confirmed:/status:/state_*` 超集词表） |
| `e2e_helpers.py` | 环境/执行 helper：`sh`/`run_step_cmd`（dcat 用 argv 防注入）、`sweep`（幂等清扫）、`provision`、`substitute`、`check_precondition`（物理前置）、`apply_assert`（基础断言算子） |
| `conftest.py` | pytest fixtures（dcat/e2e_env/tracked/recorder）+ 报告钩子，写 `report.md`/`results_*.csv`/`failures_*.log`/`$GITHUB_STEP_SUMMARY` |
| `check_cases_sync.py` | 校验 `testcases.xlsx` 结构（表头/行数）+ fault catalog 可解析 |
| `pytest.ini` | pytest 配置（testpaths/`-ra --strict-markers`） |
| `requirements.txt` | pytest / openpyxl / pytest-xdist 依赖 |
| `report.md` / `report_{worker}.md` | 每次运行的汇总报告（已 gitignore） |
| `results_<ts>.csv` | 每次运行的逐用例结果（已 gitignore） |
| `failures_<ts>.log` | 失败用例完整 stdout/stderr/verify 输出（已 gitignore） |

## 用例源（testcases.xlsx）

表结构 11 列：用例编号 / 用例标题 / 前置条件 / 测试步骤 / 测试数据 / 预期结果 / 优先级 / 关联需求 / 补充标识 / 验证观测命令 / 验证断言。由 `e2e_loader.py` 按表头名加载（容错列重排），从「测试步骤」解析 `dcat (inject|clean|query|list|serve)` 命令 argv，从「前置条件」构造前序 setup 注入。

- 共 **633** 条，其中 **618** 条含 `dcat` 命令步骤（其余为框架/配置类纯验证）。
- 优先级 P0 151 / P1 380 / P2 102：作为 pytest marker（`-m P0` 等）。
- 按标题首段生成模块 marker（如 `rNPU_bw_limit`）+ 关联需求 marker + `xdist_group`（同故障模块同 worker 串行，避免网络/进程状态冲突）：`rnet_qdisc` / `rnet_link` / `rnet_proc` / `rnet_other` / `rproc` / `rcpu` / `rmem` / `rsys` / `rfs` / `rnpu` / `misc`。

## 用法

```bash
# 全量执行（需先 cmake -B build && cmake --build build 产出 ./build/dcat）
python3 -m pytest tests/e2e/

# 只跑指定优先级 / 模块 / 冒烟
python3 -m pytest tests/e2e/ -m P0
python3 -m pytest tests/e2e/ -m rnpu_bw_limit
python3 -m pytest tests/e2e/ -m smoke

# pytest-xdist 并行（CI full 层用 -n 2 --dist loadgroup）
python3 -m pytest tests/e2e/ -n auto

# JUnit 输出（CI 用，供 ci-gate 判定）
python3 -m pytest tests/e2e/ --junitxml=tests/e2e/e2e_junit.xml
```

**非 root + sudo 提权（DCAT_AUTO_SUDO）**：CI 非 root job 置 `DCAT_AUTO_SUDO=1`，框架对 `dcat` / verify 命令自动 `sudo -n -E env HOME=$E2E_HOME` 提权，并显式把 HOME 保持到隔离目录 `$E2E_HOME`（默认 `/tmp/dcat_e2e_home`）。原因是 `sudo env_reset/always_set_home` 会把 HOME 重置为 `/root`，导致 dcat 的 state 写进 `/root/.demoncat`、而 sweep 只清 `$E2E_HOME` → 跨用例状态残留 → 重注入返回 exit 5 假 FAIL。显式 HOME 让 state 落在 sweep 能清理的隔离目录。

## 执行模型

- 每个 xlsx 用例 = 一个 pytest item。执行流：**跳过判定 → 物理前置检查 → provision（rPROC 用 `--pid` 占位注入 sleep 进程）→ 前序 setup 注入 → 按序执行 dcat 命令 → 每条命令后跑「验证观测命令」→ `eval_assert`（任一命令后通过即 PASS）**。
- **verify 机制**：每条 dcat 命令执行后，用 `case.vcmd`（验证观测命令）检查底层真实状态（`tc qdisc show` / `ss` / `cat /sys` / `iptables -L` / `hccn_tool` / `npu-smi` 等），并把输出交给 `case.vassert`（断言 DSL）判定。`state_*` 断言直接解析 `dcat query` 的 data[] / confirmed 字段。无 dcat 命令或不可解析 → 预置 SKIP。
- **前置条件（coded_pre）**：`test_e2e_cases.py` 按关键词白名单触发物理检测——`sysfs_writable` / `tc_qdisc` / `sch_tbf` / `npu_hardware` / `non-root` / `配置文件` / `mock` / `serve` / `非tmpfs` / `SSH` / `管理网卡` / `iptables` / `service_stop`（外加 `none` / `roce_link_up` 特例）。检测逻辑见 `e2e_helpers.py::check_precondition`：RoCE 链路 UP / CPU sysfs 可写且可 offline / tc + sch_tbf 模块 / iptables 可操作 / eth0 管理网卡存在（存在则 SKIP 防断网）/ NPU 的 hccn_tool + `/dev/davinci*` + eth2 / mock / serve 长超时 / service_stop 候选服务 / /tmp 非 tmpfs / non-root 降权 / 配置文件存在。另一类描述性前置（"X 服务已运行" / "X 缺失环境"）在 `test_e2e_cases.py` 中再检测。前置换环境缺失 → SKIP（NPU 用例在非 NPU 机器亦可 SKIP）。
- **sweep（幂等清扫，限 dcat 命名空间）**：session 开始时清扫上次运行/崩溃残留；每切换模块先 sweep 清理上个模块残留；inject-only 用例（只 inject 无 clean）或失败用例结束后强制 sweep。清扫范围：清 dcat 残留进程、`/tmp/dcat-*` 工件、`$HOME/.demoncat/state.json`、测试网卡 qdisc/链路复位、CPU offline 恢复、iptables 规则、NPU stale state（ip_rule/route/ip_route/link up）、dmsetup/loop 残留。sweep 脚本经临时文件执行，避免 `pkill -f 'PATTERN'` 匹配到 sweep 自身。
- **失败安全网**：任一步异常仍执行后置清扫；非 root 环境 sweep 同样走 sudo。

## 断言 DSL（验证断言列）

由 `e2e_assert.py::eval_assert` 计算（基础算子委托 `e2e_helpers.py::apply_assert`）：

| 断言 | 说明 | 作用对象 |
| --- | --- | --- |
| `exitcode:N` | 命令退出码相等 | dcat 命令 rc |
| `>=N` / `<=N` / `==N` / `!=N` | 数值比较 | 观测输出末行 |
| `eq:STR` / `ne:STR` | 字符串相等 / 不等 | 观测输出末行 |
| `contains:STR` / `notcontains:STR` | 包含 / 不包含 | 观测输出 |
| `out_contains:STR` | 命令自身 stdout 包含 | dcat 命令输出 |
| `regex:PAT` | 正则匹配 | 观测输出 |
| `empty` / `nonempty` | 输出空 / 非空 | 观测输出 |
| `exists:PATH` / `notexists:PATH` | 文件存在 / 不存在 | 文件系统 |
| `state_empty` | state 数据为空 | `dcat query` data[] |
| `state_contains:<uid>` | state 包含 uid；`<uid>` 占位符退化为「state 活跃」 | `dcat query` data[] / confirmed |
| `state_not_contains:<uid>` | state 不包含 uid；`<uid>` 占位符退化为「state 非活跃」 | `dcat query` data[] / confirmed |
| `confirmed:true` | 观测输出含 `confirmed:true`（query 确认注入生效） | 观测输出 |
| `confirmed:true→false` | clean 后 `confirmed:true` 从观测输出消失 | 观测输出 |
| `status:ok` | dcat 输出含 `status:ok` | dcat 命令输出 |
| 裸 `notexists` | 由 `test_e2e_cases.py::_eval_step` 转成 `notexists:<path>`（`ls`/`cat` 纯路径，按文件系统判）或 `notexists_rc:`（含通配符/复合命令无法定位路径，按 ls 报错标记判） | 文件系统 / 观测输出 |

不可自动判定的描述性/占位断言（全角括号开头、未映射词、含 `< >` 占位符非 state_*）→ 返回 SKIP，由 `pytest.skip()` 处理。观测输出为空时负向断言（`notcontains`/`ne`/`!=` 等）不得真空 PASS。

## 产物

- `report.md` / `report_{worker}.md`：汇总（用例总数 / PASS / FAIL / SKIP / 通过率）+ 失败用例表 + 跳过用例（pytest-xdist 每 worker 一份，`report_gw*.md`）。
- `results_<ts>.csv`：逐用例 `id / flow_id / module / command / verify_assert / expected_behavior / actual_exit_code / actual_json / result / duration_ms / timestamp` 等（含 worker 后缀）。
- `failures_<ts>.log`：失败用例完整 stdout/stderr/verify 输出 + GHA `::error::` 注解。
- CI 额外：`tests/e2e/e2e_junit.xml`（供 ci-gate 判定）+ `e2e_run.log` + 汇总追加到 `$GITHUB_STEP_SUMMARY`。

## CI 拆分（.github/workflows/pr_test.yaml）

`pr_test.yaml` 为单一入口（取代旧 pr_test_light / pr_test_full）。Light 层对 push/PR 全跑：Lint（`_pre_commit`）、Unit（`_unit_test`，x86 + arm）、e2e-light（`_e2e_test`，x86 + arm，`-m "(P0 and not hardware and not root) or (P1 and smoke and not hardware and not root)"` 用户态冒烟，非 root + DCAT_AUTO_SUDO）。

Full 层仅在源仓库 + push 或 PR 带 ready / ready-for-test 标签时跑，非 NPU / NPU 并行：

| Job | pytest `-m` 筛选 | runner |
| --- | --- | --- |
| e2e-full-ubuntu-nonnet | `not hardware and not net` | ubuntu-24.04-arm（非 root） |
| e2e-full-ubuntu-qdisc | `not hardware and (rnet_bw_limit or rnet_degrade or rnet_jitter or rnet_reorder or rnet_delay or rnet_loss)` | ubuntu-24.04-arm（非 root） |
| e2e-full-ubuntu-netother | `not hardware and net and not (rnet_bw_limit or rnet_degrade or rnet_jitter or rnet_reorder or rnet_delay or rnet_loss)` | ubuntu-24.04-arm（非 root） |
| e2e-full-npu | `hardware` | self-hosted NPU（run-as-root） |

clang-tidy / coverage 为建议性（仅报告不阻断）。`ci-gate` 汇总各 job 结果：lint / unit / e2e-light / e2e-full-npu 须成功或跳过；full ubuntu 三 job 若标失败，下载各自 `e2e_junit.xml` 按 `<failure>` / `<error>` 元素实际计数判定是否真失败（规避 ARM runner 断连伪失败）。`_e2e_test.yaml` 为 E2E 复用 workflow（inputs：runner / mark / run-as-root / ref / artifact-name / xdist）。

## 参考

- 测试设计：[docs/DESIGN.md §11](../../docs/DESIGN.md)
- 技术规格：[SPEC.md §9](../../SPEC.md)
- 项目主页：[README.md](../../README.md)