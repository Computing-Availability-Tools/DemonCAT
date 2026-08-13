# DemonCAT — 仓库上下文与审视指南

## 这是什么

DemonCAT 是 C 语言故障注入工具(网络/NPU/CPU/存储/进程),用于混沌与韧性测试。
核心设计:故障 = 外部脚本 + 声明式 config 接入,加一个故障 = 加脚本 + 配置项,免重新编译 C。

## 目录结构

- `src/core/` — 核心 C(types/cli/registry/executor/precheck/state/config/output/serve/dispatch/reinject/help)
- `src/injectors/`、`src/plugins/` — 注入器与插件管理
- `config/` — 声明式故障配置(`demoncat.conf`)
- `src/scripts/` — 故障外部脚本
- `tests/` — 单测(ctest) + e2e(`cases.csv` + `run_e2e.py`)
- `third_party/cjson/` — vendored(免审)
- `.github/workflows/` — CI(pr_test/e2e/unit/clang-tidy/coverage/release)
- `scripts/install_deps.sh` — 依赖安装

## 构建

- CMake, C11, `-Wall -Wextra -Werror`
- `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
- 全局 `-D_POSIX_C_SOURCE=200809L`;用 `realpath` 等 POSIX 函数前须 `#define _GNU_SOURCE` 在所有系统头之前

## PR 标题规范(强制,由 pre-commit 校验)

PR 标题须符合 Conventional Commits:

```text
<type>(<scope>): <desc>
```

允许的 type:

```text
feat | fix | docs | style | refactor | perf | test | build | ci | chore | revert | merge
```

示例:

```text
feat(npu): add rNPU_port_occupy fault
fix(state): clean idempotency on corrupt state.json
ci: add problem matchers for actionlint/markdownlint
```

## 代码审视要点(opencode review 时遵循)

### 输出

- 评论语言:**中文**。审视建议/标题/摘要建议放在三反引号代码块里便于复制。只针对本 PR 的 diff 给可执行建议,避免泛泛而谈。
- 只提有意义的建议,避免吹毛求疵的 nitpick(对齐原 Gemini `HIGH` 阈值降噪意图)。
- 开头先给一句话变更摘要,再给逐条审视意见(对齐原 Gemini `summary` 行为)。

### 代码规范(C11)

- 语言标准 **C11(ISO/IEC 9899:2011)**,CMake 构建;**不要建议** C++、GNU 扩展或非标准特性。
- 退出码约定:

  ```text
  0 成功
  1 运行错误(脚本失败等)
  2 解析错误(命令格式不合法)
  3 预检拒绝(参数缺失/不合法/op 不支持)
  4 未找到(uid 不在目录)
  5 重注入拒绝(同资源已注入,需 --force)
  ```

  预检拒绝应返回 `3`,而非笼统返回 `1`。
- 第三方 `third_party/cjson`(vendored)与 `build/` 产物不在审视范围,勿对其提建议。

### 状态引擎不变量(务必在审视中检查回归)

- `clean` 须**幂等**:重复 clean 同一资源不报错、状态一致。
- `state.json` 丢失/损坏时仍可 clean(stateless 回退,清 `/tmp` 工件)。
- 同资源已注入须**拒绝重注入**(返回 `5`),仅在 `--force` 时允许替换。
- `clean --all` 须 fan-out 到全部支持 clean 的故障(stateless 无参 clean)。
- `query` 须幂等:有 uid 校验故障是否生效,无 uid 查全部活跃记录。

### 内存与并发

- `pthread` 锁保护 state;审视须查**泄漏、UAF、double-free、死锁、TOCTOU、信号安全**。
- 资源(handle/fd/锁)在错误路径上须正确释放(goto cleanup 模式可接受)。
- 动态插件 `dlopen` 路径须查符号校验、句柄关闭、与主程序的符号隔离。

### 架构约束

- 故障以"**外部脚本 + 声明式 config**"接入(`config/`、`src/scripts/`);**不要建议把具体故障逻辑硬编码进 C**——这正是本仓"加一个故障 = 加脚本 + 配置一行"的设计核心。
- Web 控制面 `dcat serve` 为只读默认,`--allow-write` 才开注入/清理;审视须注意写入接口的鉴权。

### 审视优先级

1. 状态机幂等性与韧性(clean 幂等 / 孤儿恢复 / clean --all fan-out)。
2. 退出码语义正确(预检拒绝走 `3`,未找到走 `4`,重注入走 `5`)。
3. C 内存安全 + pthread 并发安全。
4. PR 是否带对应单测(`tests/` 下 ctest)或 E2E 用例(`tests/e2e/cases.csv`)。
