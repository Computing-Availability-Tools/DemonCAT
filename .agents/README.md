# DemonCAT skills

本目录存放 DemonCAT 的 **Agent Skill**。

> **如何使用**：`.agents/skills` 是工具中立的存放处——各 agent 运行时默认不会扫描它。把它下面的技能**拷贝或软链**到你所用运行时的目录即可：
>
> | 运行时 | 目标目录 |
> | --- | --- |
> | opencode | 项目 `.opencode/skills/`，或全局 `~/.config/opencode/skills/` |
> | Claude Code | 项目 `.claude/skills/`，或全局 `~/.claude/skills/` |
> | 通用别名 | `~/.agents/skills/` |
>
> ```bash
> # 软链示例（随仓库更新）
> ln -s "$(pwd)/.agents/skills/using-demoncat" ~/.config/opencode/skills/using-demoncat
> ```
>
> 也可以走 npm（内容与仓库同源、单独发布）：`npx demoncat-skill install`

## 目录

- `skills/` — 技能本体。每个子目录是一个**自洽子技能**（`SKILL.md` + `references/` + `scripts/`）。

## `using-demoncat` — DemonCAT 故障注入

把"帮我注入网络延迟 / 把 CPU 打满 / 清理故障"这类自然语言意图，翻译成正确的 `dcat` 命令，并**用可观测证据验证**是否真的生效。

### 做什么

- 把意图映射到 `dcat list / inject / clean / query`（含参数与常见场景）；
- **防错觉**：成功**只认** `dcat query` 输出里的 `data.confirmed: true`（`dcat query` 的退出码 0 ≠ 故障生效）；
- **能力以 `dcat list` 为唯一事实来源**：目录里没有的故障就如实说"没有"，绝不编造 uid/参数；
- 危险 / 不可逆故障的**确认与恢复**（☠️ 不可逆 / ⚠️ 高风险）；
- 附 `scripts/verify.sh` 取证脚本。

### 文件布局

| 文件 | 作用 |
| --- | --- |
| `SKILL.md` | 技能正文（**稳定**，不含会变的 uid/参数清单） |
| `references/modules.md` | 模块 ↔ uid 前缀 ↔ 常见参数（**生成物**） |
| `references/quick-commands.md` | 每个故障的命令示例（**生成物**） |
| `references/fault-catalog.md` | 完整参数 + 危险标注（**生成物**） |
| `references/safety.md` | 危险/不可逆故障的恢复流程与硬约束（稳定） |
| `scripts/verify.sh` | 用 `dcat query` 判定故障是否生效的取证脚本 |
| `scripts/gen-catalog.js` | 数据生成器：从 `config/demoncat.conf` + `User_Manual.md` 重新生成上面三份 references（Node） |

### 触发

直接说需求即可，例如："用 DemonCAT 在 eth0 注入 200 毫秒延迟并验证"、"把 CPU 全核打满"、"清理这台机器上的所有故障注入"。

### 关键约束

- **SKILL.md 固定、数据生成**：`references/` 的三份生成物由 DemonCAT 侧的 `gen-catalog` 从工具（`config/demoncat.conf` + `User_Manual.md`）产出，发版时刷新——**加/改故障不需要改 skill**。
- **成功 = `confirmed:true`**；`dcat query` 退出码 0 只代表命令跑通。
- **inject-only 故障（无 `query` op）无法验证**：`verify.sh` 返回 `NOT_VERIFIABLE`，需二次确认，不得谎报成功。
- **危险分级**：☠️ = 该故障无 `clean`（不可逆，自动推导）；⚠️ = `User_Manual.md` 危险等级为「高/极高」。
