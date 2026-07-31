# DemonCAT E2E 测试

CSV 驱动的 `dcat` 二进制端到端测试框架：自动生成用例 → 串行执行 → linux 命令观测断言 → 输出报告。

## 文件

| 文件 | 说明 |
|---|---|
| `gen_cases.py` | 从 `config/demoncat.conf` 故障目录 + 内置观测/边界/安全知识自动生成 `cases.csv` |
| `cases.csv` | 用例表（仅期望列；改目录后重生成即可） |
| `run_e2e.py` | 执行框架（读 cases.csv → 跑 → 写 results/report） |
| `results_<时间戳>.csv` | 每次运行产物（含实际结果列，已 gitignore） |
| `report.md` | 每次运行的逐用例报告（已 gitignore） |

## 用例矩阵（cases.csv）

| 分类 | 覆盖 |
|---|---|
| F | 功能基线：37 故障 inject→verify→clean→query 无幽灵 |
| B | 边界值：参数 valid/invalid（cores/load_pct/port/chip） |
| H | 主机安全：危险资源无守卫确认、写入边界（device=/tmp 安全路径） |
| P | 权限边界：非 root 跑 root 故障 → 拒绝 + 无半成品（inject 步 `runuser -u nobody` 降权） |
| I | 命令注入：每参数塞良性 shell 载荷，验证未执行（dcat 命令 argv 执行，载荷原样进 cli_parse） |
| R | 自愈/一键恢复：state 删除/损坏/孤儿/幽灵/clean --all 幂等 |
| S | 状态一致性与幂等：clean×2 / --force×2 / query×2 |

## 用法

```bash
# 1. 生成/更新用例（改 config/demoncat.conf 或观测映射后重跑）
python3 tests/e2e/gen_cases.py

# 2. 执行（生产全量跑，不 skip）
python3 tests/e2e/run_e2e.py                 # 当前用户权限跑
sudo python3 tests/e2e/run_e2e.py           # root：跑全含 tc/iptables/sysfs 等

# 只跑指定 flow（逗号分隔前缀）
python3 tests/e2e/run_e2e.py --flows F-rCPU_overload,B-,I-

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

- **NPU 20 条**：无 `hccn_tool` → FAIL（需 Atlas NPU 物理机）。
- **rCPU_core_offline**：默认实跑，瞬态下线真实核 `cpu1`（clean+清扫恢复）。
- **P 类**：root 框架下用 `runuser -u nobody` 降权验证非 root 被拒绝。
- **H-3 写入边界**：`device=/tmp` 安全路径（不污染 `/etc`）。

## 产物

- `results_<时间戳>.csv`：每步 `actual_exit_code/actual_json/verify_actual/result/error_code/duration/timestamp`。
- `report.md`：汇总 + 分类统计 + 失败用例 + 跳过原因。
- `test_report.md` §10：追加 e2e 汇总段（`--no-append` 可关）。

## 断言 DSL（verify_assert 列）

`>=N <=N ==N !=N`（数值）、`eq:STR ne:STR`（串）、`contains:STR notcontains:STR`、`regex:PAT`、`empty nonempty`、`exists:PATH notexists:PATH`、`exitcode:N`、`state_empty state_contains:uid state_not_contains:uid`。
