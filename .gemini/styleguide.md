# DemonCAT 代码审视风格指南

> 供 Gemini Code Assist 在审视本仓库 PR 时遵循。仓库: DemonCAT — C 故障注入工具(故障以"外部脚本 + 声明式 config"接入,免重新编译)。

## 输出要求

- 审视建议、PR 标题/摘要建议 **必须放在三反引号代码块里**,便于复制。
- 评论语言: **中文**(本仓库文档与代码注释以中文为主)。
- 只针对本次 PR 的 diff 给出可执行建议,避免泛泛而谈。

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

## 代码规范(C11)

- 语言标准 **C11(ISO/IEC 9899:2011)**,CMake 构建;**不要建议** C++、GNU 扩展或非标准特性。
- 错误处理须遵循本仓退出码约定:

  ```text
  0 成功
  1 运行错误(脚本失败等)
  2 解析错误(命令格式不合法)
  3 预检拒绝(参数缺失/不合法/op 不支持)
  4 未找到(uid 不在目录中)
  5 重注入拒绝(同资源已注入,需 --force)
  ```

  预检拒绝应返回 `3`,而非笼统返回 `1`。

- 第三方 `third_party/cjson`(vendored)与 `build/` 产物 **不在审视范围**,勿对其提建议。

## 状态引擎不变量(务必在审视中检查回归)

- `clean` 须**幂等**:重复 clean 同一资源不报错、状态一致。
- `state.json` 丢失/损坏时仍可 clean(stateless 回退,清 `/tmp` 工件)。
- 同资源已注入须**拒绝重注入**(返回 `5`),仅在 `--force` 时允许替换。
- `clean --all` 须 fan-out 到全部支持 clean 的故障(stateless 无参 clean)。
- `query` 须幂等:有 uid 校验故障是否生效,无 uid 查全部活跃记录。

## 内存与并发

- `pthread` 锁保护 state;审视须查**泄漏、UAF、double-free、死锁、TOCTOU、信号安全**。
- 资源(handle/fd/锁)在错误路径上须正确释放(goto cleanup 模式可接受)。
- 动态插件 `dlopen` 路径须查符号校验、句柄关闭、与主程序的符号隔离。

## 架构约束

- 故障以"**外部脚本 + 声明式 config**"接入(`config/`、`src/scripts/`);**不要建议把具体故障逻辑硬编码进 C**——这正是本仓"加一个故障 = 加脚本 + 配置一行"的设计核心。
- Web 控制台 `dcat serve` 为只读默认,`--allow-write` 才开注入/清理;审视须注意写入接口的鉴权。

## 审视优先级

1. 状态机幂等性与韧性(clean 幂等 / 孤儿恢复 / clean --all fan-out)。
2. 退出码语义正确(预检拒绝走 `3`,未找到走 `4`,重注入走 `5`)。
3. C 内存安全 + pthread 并发安全。
4. PR 是否带对应单测(`tests/` 下 ctest)或 E2E 用例(`tests/e2e/cases.csv`)。

## 命令

- `/gemini review` 对当前 PR 状态重审。
- `/gemini summary` 出变更摘要。
- `@gemini-code-assist <问题>` 在评论里追问(可附 PR 上下文)。
- `/gemini help` 列出可用命令。
