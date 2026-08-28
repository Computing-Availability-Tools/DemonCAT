# DemonCAT

> **DemonCAT** — Demon Computing Availability Tools，专注于计算故障注入工具。

覆盖 CPU / 存储 / 网络 / 进程 / 内存 / 文件系统 / Docker / NPU / 系统模块，提供统一的命令面、预检护栏、状态跟踪；具体故障以**外部脚本 + 声明式配置**接入。

加一个故障 = 加一个脚本 + 配置文件一行，**免重新编译**。

| 项目 | 说明 |
| ------ | ------ |
| 版本号 | v0.1.1 |
| 发布时间 | 2026-08-11 |
| 许可证 | Apache-2.0 |

## 依赖说明

极简 Linux 环境（最小安装/容器）可能不自带以下工具。运行 `scripts/install_deps.sh` 一键安装编译和运行依赖。

### 编译依赖

| 依赖 | 包名 (apt) | 包名 (yum) | 用途 |
| --- | --- | --- | --- |
| cmake ≥ 3.10 | `cmake` | `cmake` | 构建系统 |
| C 编译器 | `gcc` | `gcc` | 编译 dcat 二进制 |
| pthread | `libc6-dev` | `glibc-devel` | 状态锁 |
| dlopen | `libc6-dev` | `glibc-devel` | 动态插件加载 |

### 运行时依赖（按模块）

| 模块 | 工具 | 包名 (apt) | 包名 (yum) | 需要 root |
| --- | --- | --- | --- | --- |
| **CPU** | `perl`, `taskset` | `perl`, `util-linux` | `perl`, `util-linux` | core_offline 需要 |
| **存储** | `dd` | `coreutils` | `coreutils` | — |
| **网络** | `tc`, `ip` | `iproute2` | `iproute` | ✅ |
| | `iptables` | `iptables` | `iptables` | ✅ |
| | `systemctl` | `systemd` | `systemd` | ✅ |
| | `python3` | `python3` | `python3` | — |
| **进程** | `kill`, `perl` | `util-linux`, `perl` | `util-linux`, `perl` | 部分需要 |
| **内存** | `perl` / `python3` | `perl` / `python3` | `perl` / `python3` | — |
| **文件系统** | `mkfs`, `mount` | `e2fsprogs`, `util-linux` | `e2fsprogs`, `util-linux` | ✅ |
| **Docker** | `docker` | `docker.io` | `docker` | ✅ |
| **NPU** | `hccn_tool`, `npu-smi`, `setpci` | — (Atlas 驱动自带), `pciutils` | — | ✅ |
| **系统** | `sysctl`, `date` | `procps`, `coreutils` | `procps-ng`, `coreutils` | ✅ |

> 无 NPU 硬件的环境可跳过 NPU 模块，不影响其他模块使用。

## 快速开始

```bash
# 1. 一键安装依赖（Debian/Ubuntu/RHEL/CentOS 自动识别）
bash scripts/install_deps.sh

# 2. 编译并创建全局入口（8核并行加速；需要 sudo）
mkdir -p build && cd build && cmake .. && make -j8 && sudo make install && cd ..

# 3.（可选）运行测试
cd build && ctest --output-on-failure

# 4. 列出故障目录
dcat list

# 5. 注入 CPU 过载（2 核）
dcat inject rCPU_overload --cores=0,1

# 6. 查询故障是否生效
dcat query rCPU_overload --cores=0,1

# 7. 清除故障
dcat clean rCPU_overload --cores=0,1

# 查看帮助
dcat --help
dcat inject --help
dcat inject rCPU_overload --help
```

> **提示**：本文档所有命令均以 `dcat` 形式书写。若未执行上方安装（或无 sudo 权限），请将 `dcat` 替换为 `./build/dcat`。

E2E 测试（pytest + testcases.xlsx 驱动，633 用例）详见 [tests/e2e/README.md](tests/e2e/README.md) 与 [docs/DESIGN.md](docs/DESIGN.md)。

## 命令格式

```bash
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

| 子命令 | 说明 | 示例 |
| --- | --- | --- |
| `inject <uid> --p1=v1 ...` | 注入故障，同步阻塞执行 | `dcat inject rCPU_overload --cores=4` |
| `clean <uid> [--k1=v1 ...]` | 带参数：按参数匹配清除活跃注入；无参数：stateless 清该 uid 全部 `/tmp` 工件（state.json 丢失仍可清） | `dcat clean rCPU_overload --cores=4` / `dcat clean rCPU_overload` |
| `clean --all` | 对全部支持 clean 的故障 fan-out 无参 clean（stateless） | `dcat clean --all` |
| `query [uid] [--k1=v1 ...]` | 无 uid：查全部活跃记录；有 uid：验证故障生效 | `dcat query` / `dcat query rCPU_overload` |
| `list` | 列出故障目录 | `dcat list` |
| `serve [--port N] [--bind ADDR] [--webroot DIR] [--allow-write]` | 启动 HTTP 控制平面（长驻）：静态前端 + `/api/*`；默认只读，`--allow-write` 开注入/清理 | `dcat serve --port 8080 --allow-write` |

详细使用手册见 [User_Manual.md](User_Manual.md)，技术规格见 [SPEC.md](SPEC.md)，架构设计见 [docs/DESIGN.md](docs/DESIGN.md)。

## Web 控制台（dcat serve）

`dcat serve` 在二进制内内置 HTTP 控制平面 + 静态前端（`src/web/`），把故障目录、活跃注入、历史记录搬到浏览器；默认只读，`--allow-write` 开注入/清理，无外部 HTTP 依赖。

```bash
./build/dcat serve --port 8080 --allow-write
```

详见 [User_Manual.md](User_Manual.md)。

## 退出码

| 码 | 含义 |
| --- | --- |
| 0 | 成功 |
| 1 | 运行错误（脚本失败等） |
| 2 | 解析错误（命令格式不合法） |
| 3 | 预检拒绝（参数缺失/不合法/op 不支持） |
| 4 | 未找到（uid 不在目录中） |
| 5 | 重注入拒绝（同资源已注入，需 `--force`） |

## 技术栈

- C11（ISO/IEC 9899:2011），CMake 构建
- cJSON（vendored 单文件库）
- pthread（状态锁）
- INI 配置文件（`demoncat.conf`）
- 输出格式：JSON（`list` 为可读文本表格）

## 文档

| 文档 | 说明 |
| ------ | ------ |
| [SPEC.md](SPEC.md) | 功能规格说明书 |
| [User_Manual.md](User_Manual.md) | 使用手册 |
| [docs/DESIGN.md](docs/DESIGN.md) | 架构设计 |
| [docs/DemonCAT_Error_List.md](docs/DemonCAT_Error_List.md) | 故障目录（58 条） |
| [docs/Test_Report.md](docs/Test_Report.md) | 测试报告 |
| [docs/Dynamic_Plugin_Implement.md](docs/Dynamic_Plugin_Implement.md) | 动态插件开发指南 |
| [docs/Manual_Test_Reference.md](docs/Manual_Test_Reference.md) | 手动测试参考 |
| [Release_Notes.md](Release_Notes.md) | 版本发布记录 |
