# DemonCAT
DemonCAT 是 CAT (Computing Availability Tools) 系列软件之一，是一款主要用于服务器及AI计算平台的故障注入工具，支撑系统可靠性验证能力覆盖。

## 编译与运行

### 环境要求

DemonCAT 使用 C11（gnu11 扩展）开发，依赖 POSIX 环境（Linux/WSL），不支持原生 Windows 编译。运行时依赖 `/proc/self/exe` 定位配置、`fork`/`exec`/`pipe`/`timer_create` 等系统调用。

### 第三方依赖

| 依赖 | 版本 | 用途 | 来源 |
|---|---|---|---|
| GCC（C 编译器） | ≥ 9（建议 13+） | C11/gnu11 编译，使用 `fork`/`exec`/`pipe`/`timer_create`/`usleep` 等 POSIX 扩展 | 系统包 `build-essential` |
| CMake | ≥ 3.10 | 构建系统，生成 Makefile + 驱动 `ctest` 单元测试 | 系统包 `cmake` |
| pthread | — | 线程互斥锁（`state.c` 的 `pthread_mutex_t` 保护注入记录并发安全） | glibc 自带，CMake `find_package(Threads)` |
| cJSON | v1.7.18 | JSON 构造/解析（`output.c` 的 result_t 输出、`state.c` 的 state.json 持久化、`dispatch.c` 的 list/query） | **vendored**，随仓库分发 `third_party/cjson/cJSON.{c,h}`（MIT 许可） |
| dl（动态加载） | — | `dlopen`/`dlsym`/`dlclose` 加载动态插件 `.so`（三层扩展架构的第 3 层） | glibc 自带，CMake `${CMAKE_DL_LIBS}` |

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

# 帮助
./build/dcat --help
```

### 全局选项

| 选项 | 说明 |
|---|---|
| `--config <path>` | 指定 `demoncat.conf` 路径（默认 `<binary_dir>/../config/demoncat.conf`，通过 `/proc/self/exe` 解析） |
| `--plugins <dir>` | 指定动态插件目录（默认 `<root>/plugins`） |
| `--help` | 打印帮助 |

### 故障目录与扩展机制

- **38 条内置故障**：`config/demoncat.conf` 声明，脚本位于 `src/scripts/<module>/`（cpu/network/process/storage/npu）
- **三层扩展架构**：
  1. cnf + 脚本（数据驱动，免重编译，加脚本+cnf 段即新增故障）
  2. 编译注入器 `injector_t`（进程内高级扩展点，`src/injectors/`）
  3. 动态插件 `dcat_plugin_t`（dlopen .so，`src/plugins/`，ABI 版本 + 生命周期 + 元数据驱动预检）

更多设计详见 [SPEC.md](SPEC.md) 与 [DESIGN.md](DESIGN.md)。
