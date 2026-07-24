# DemonCAT
DemonCAT 是 CAT (Computing Availability Tools) 系列软件之一，是一款主要用于服务器及AI计算平台的故障注入工具，支撑系统可靠性验证能力覆盖。

## 核心特性

- **38 条故障目录**：覆盖 cpu / network / process / storage / npu 五大模块
- **三层扩展架构**：cnf+脚本（数据驱动）→ 编译注入器（进程内）→ 动态插件（dlopen .so）
- **数据驱动开闭原则**：新增故障只需加脚本 + `demoncat.conf` 段，免重编译
- **统一 JSON 输出**：所有命令输出结构化 JSON，便于自动化集成
- **state 持久化**：注入记录跨进程持久化（`~/.demoncat/state.json`），支持按参数匹配清除
- **子命令式 CLI**：`dcat <sub> [uid] --key=value ...`，参数经环境变量传脚本

## 目录结构

```
DemonCAT/
├── src/
│   ├── core/          # 核心模块
│   │   ├── types.{c,h}        # 公共类型：params_t/result_t/fault_def_t/injection_record_t
│   │   ├── cli.{c,h}          # argv 子命令解析（--key=value）
│   │   ├── config.{c,h}       # INI 解析 + 项目根/脚本路径推导
│   │   ├── registry.{c,h}     # fault_def 静态表 + find/list
│   │   ├── executor.{c,h}     # fork/exec+pipe 同步执行 + system 直通 + mock 钩子
│   │   ├── precheck.{c,h}     # 4 步预检 + 未声明参数拒绝（通用化）
│   │   ├── state.{c,h}        # 注入记录 + pthread 互斥 + cJSON 持久化
│   │   ├── output.{c,h}      # result_ok/err JSON schema
│   │   ├── dispatch.{c,h}     # op 路由 + 三级回退（cnf→injector→plugin）
│   │   └── ... 
│   ├── injectors/     # 编译注入器（进程内高级扩展点，builtin_injectors[] 留位）
│   ├── plugins/       # 动态插件（plugin.h 接口 + plugin_manager + sample 示例）
│   ├── scripts/       # 38 条故障脚本（cpu/network/process/storage/npu）
│   └── main.c         # 编排：config→registry→state→plugins→cli→dispatch→output
├── config/
│   └── demoncat.conf  # 故障目录配置（38 条声明）
├── tests/             # 13 个单元测试
├── third_party/cjson/ # cJSON 单文件库（vendored，MIT）
└── CMakeLists.txt
```

## 编译与运行

### 环境要求

DemonCAT 使用 C11（gnu11 扩展）开发，依赖 POSIX 环境（Linux/WSL），不支持原生 Windows 编译。运行时依赖 `/proc/self/exe` 定位配置、`fork`/`exec`/`pipe`/`timer_create` 等系统调用。

### 第三方依赖

| 依赖 | 版本 | 用途 | 来源 |
|---|---|---|---|
| GCC（C 编译器） | ≥ 9（建议 13+） | C11/gnu11 编译，使用 `fork`/`exec`/`pipe`/`timer_create`/`usleep`/`strtok_r` 等 POSIX/GNU 扩展 | 系统包 `build-essential` |
| CMake | ≥ 3.10 | 构建系统，生成 Makefile + 驱动 `ctest` 单元测试 + 编译动态插件 `.so`（MODULE 库） | 系统包 `cmake` |
| pthread | — | 线程互斥锁（`state.c` 的 `pthread_mutex_t` 保护注入记录并发安全） | glibc 自带，CMake `find_package(Threads)` |
| cJSON | v1.7.18 | JSON 构造/解析：`output.c` 的 result_t 输出、`state.c` 的 state.json 持久化、`dispatch.c` 的 list/query 输出、record_id 注入 | **vendored**，随仓库分发 `third_party/cjson/cJSON.{c,h}`（MIT 许可） |
| dl（动态加载） | — | `dlopen`/`dlsym`/`dlclose` 加载动态插件 `.so`（三层扩展架构第 3 层），`dirent.h` 扫描插件目录 | glibc 自带，CMake `${CMAKE_DL_LIBS}` |

> cJSON 已 vendoring 到 `third_party/cjson/`，**无需额外下载**；其余依赖均为标准 Linux 开发工具链，通过系统包管理器安装即可。

### 安装依赖（WSL Ubuntu 示例）

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake
```

### 编译

```bash
cmake -B build && cmake --build build
```

构建产物：

| 产物 | 说明 |
|---|---|
| `build/dcat` | 主二进制 |
| `plugins/libsample.so` | 示例动态插件（验证三层扩展架构：cnf → 编译注入器 → dlopen 插件） |
| `build/test_*` | 13 个单元测试可执行文件 |

### 运行测试

```bash
ctest --test-dir build --output-on-failure
```

预期：13/13 全绿（详见 `docs/superpowers/reports/2026-07-23-e2e-test-report.md`）。

## 命令行使用

```
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--plugins <dir>] [--help]
```

### 子命令

| 子命令 | 用法 | 说明 |
|---|---|---|
| `inject` | `dcat inject <uid> --p1=v1 ...` | 注入故障；可恢复故障写 state + 返回 record_id；inject-only 不写 state |
| `clean` | `dcat clean <uid> [--k1=v1 ...]` | 清除注入；按用户参数匹配活跃记录，逐条执行 clean，失败停止 |
| `query` | `dcat query [uid] [--k=v ...]` | 无 uid 查询全部活跃记录（state 回答）；有 uid 走脚本 query 直通 stdout + confirmed |
| `list` | `dcat list` | 列出故障目录（cnf + 动态插件） |

### 全局选项

| 选项 | 说明 |
|---|---|
| `--config <path>` | 指定 `demoncat.conf` 路径（默认 `<binary_dir>/../config/demoncat.conf`，通过 `/proc/self/exe` 解析） |
| `--plugins <dir>` | 指定动态插件目录（默认 `<root>/plugins`） |
| `--help` | 打印帮助 |

### 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 脚本执行失败 / 无活跃注入可清除 |
| 2 | 命令行解析错误 |
| 3 | 预检失败（op 不支持 / 必填参数缺失 / 脚本不可执行 / 未声明参数 / inject-only 拒绝 clean/query） |
| 4 | uid 未找到（cnf + 编译注入器 + 动态插件三层均未命中） |

### 输出格式

统一 JSON，成功：`{"status":"ok","op":"...","uid":"...","data":{"message":"...","record_id":N}}`；失败：`{"status":"error","op":"...","uid":"...","error":{"code":N,"message":"..."}}`。inject-only 故障成功输出无 `record_id` 字段。

### 运行示例

```bash
# 列出故障目录（38 条 cnf + 动态插件）
./build/dcat list

# 注入 CPU 过载故障
./build/dcat inject rCPU_overload --cores=4

# 查询活跃注入（无 uid 查全部，有 uid 走脚本 query）
./build/dcat query
./build/dcat query rNET_delay --iface=eth0 --delay_ms=100

# 清除注入（按参数匹配活跃记录）
./build/dcat clean rCPU_overload --cores=4

# inject-only 故障（无 clean/query）
./build/dcat inject rPROC_exit --pid=12345
./build/dcat clean rPROC_exit    # 退出码 3 拒绝

# 动态插件故障（三级回退命中）
./build/dcat inject rSAMPLE_test
./build/dcat clean rSAMPLE_test

# 帮助
./build/dcat --help
```

## 故障目录

38 条内置故障，按模块组织（脚本位于 `src/scripts/<module>/`）：

| 模块 | 数量 | 示例 uid |
|---|---|---|
| cpu | 2 | `rCPU_overload`、`rCPU_core_offline` |
| network | 11 | `rNET_delay`、`rNET_loss`、`rNET_down`、`rNET_bw_limit` … |
| process | 4 | `rPROC_exit`（inject-only）、`rPROC_hang`、`rPROC_dstate`、`rPROC_zstate` |
| storage | 1 | `rDISK_write_overload` |
| npu | 20 | `rNPU_link_down`、`rNPU_ip_change`、`rNPU_route_add` … |

完整字段（uid/module/supported_ops/required_params/optional_params）见 `config/demoncat.conf` 与 [SPEC.md](SPEC.md) §3。

## 扩展机制（三层架构）

故障请求经 dispatch 三级回退查找：

```
dispatch_route(uid, op, params)
  ├─ 第1层 registry_find(uid)     cnf 数据驱动（脚本，免重编译）   [默认 38 条]
  ├─ 第2层 injector_find(uid)    编译注入器（进程内 builtin）     [留位]
  └─ 第3层 plugin_find(uid)       动态插件（dlopen .so）          [运行时可插拔]
     └─ 未命中 → 退出码 4
```

1. **cnf + 脚本**：加脚本到 `src/scripts/<module>/` + `demoncat.conf` 加 `[fault.<uid>]` 段即可，免重编译
2. **编译注入器 `injector_t`**：进程内高级扩展点（`src/injectors/`），用于脚本难胜任的场景，需重编译
3. **动态插件 `dcat_plugin_t`**：dlopen `.so` 运行时可插拔（`src/plugins/`），含 ABI 版本门控 + init/fini 生命周期 + 元数据驱动通用预检

更多设计详见 [SPEC.md](SPEC.md) 与 [DESIGN.md](DESIGN.md)。
