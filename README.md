# DemonCAT (dcat)

> **DemonCAT**（简称 **dcat**）— Demon Computing Availability Tools，专注于计算故障注入工具。

覆盖 CPU / 存储 / 网络 / 进程 / NPU 模块，提供统一的命令面、预检护栏、状态跟踪；具体故障以**外部脚本 + 声明式配置**接入。

加一个故障 = 加一个脚本 + 配置文件一行，**免重新编译**。

## 依赖说明

极简 Linux 环境（最小安装/容器）可能不自带以下工具。运行 `scripts/install_deps.sh` 一键安装编译和运行依赖。

### 编译依赖

| 依赖 | 包名 (apt) | 包名 (yum) | 用途 |
|---|---|---|---|
| cmake ≥ 3.10 | `cmake` | `cmake` | 构建系统 |
| C 编译器 | `gcc` | `gcc` | 编译 dcat 二进制 |
| pthread | `libc6-dev` | `glibc-devel` | 状态锁 |
| dlopen | `libc6-dev` | `glibc-devel` | 动态插件加载 |

### 运行时依赖（按模块）

| 模块 | 工具 | 包名 (apt) | 包名 (yum) | 需要 root |
|---|---|---|---|---|
| **CPU** | `perl`, `taskset` | `perl`, `util-linux` | `perl`, `util-linux` | core_offline 需要 |
| **存储** | `dd` | `coreutils` | `coreutils` | — |
| **网络** | `tc`, `ip` | `iproute2` | `iproute` | ✅ |
| | `iptables` | `iptables` | `iptables` | ✅ |
| | `systemctl` | `systemd` | `systemd` | ✅ |
| | `python3` | `python3` | `python3` | — |
| **进程** | `kill`, `perl` | `util-linux`, `perl` | `util-linux`, `perl` | 部分需要 |
| **NPU** | `hccn_tool` | — (Atlas 驱动自带) | — | ✅ |

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

## 命令格式

```
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

| 子命令 | 说明 | 示例 |
|---|---|---|
| `inject <uid> --p1=v1 ...` | 注入故障，同步阻塞执行 | `dcat inject rCPU_overload --cores=4` |
| `clean <uid> [--k1=v1 ...]` | 带参数：按参数匹配清除活跃注入；无参数：stateless 清该 uid 全部 `/tmp` 工件（state.json 丢失仍可清） | `dcat clean rCPU_overload --cores=4` / `dcat clean rCPU_overload` |
| `clean --all` | 对全部支持 clean 的故障 fan-out 无参 clean（stateless） | `dcat clean --all` |
| `query [uid] [--k1=v1 ...]` | 无 uid：查全部活跃记录；有 uid：验证故障生效 | `dcat query` / `dcat query rCPU_overload` |
| `list` | 列出故障目录 | `dcat list` |
| `serve [--port N] [--bind ADDR] [--webroot DIR] [--allow-write]` | 启动 HTTP 控制平面（长驻）：静态前端 + `/api/*`；默认只读，`--allow-write` 开注入/清理 | `dcat serve --port 8080 --allow-write` |

详细使用手册见 [docs/user_manual.md](docs/user_manual.md)，技术规格见 [SPEC.md](SPEC.md)，架构设计见 [DESIGN.md](DESIGN.md)。

## Web 控制台（dcat serve）

`dcat serve` 在二进制内内置一个 HTTP 控制平面 + 静态前端（`src/web/`），把故障目录、活跃注入、历史记录搬到浏览器：远程查看状态、构造命令、（可选）直接注入/清理。无外部 HTTP 依赖，手写 HTTP/1.1。

### 启动与访问

```bash
# 只读模式（默认）：浏览器查看 + 复制命令到 SSH 终端执行
./build/dcat serve --port 8080

# 可写模式：浏览器可直接注入/清理（注入前二次确认）
./build/dcat serve --port 8080 --allow-write

# 后台长驻（setsid 脱离终端）
setsid ./build/dcat serve --port 8080 --allow-write &

# 停止
pkill -f 'dcat serve'
```

启动后浏览器打开 `http://192.168.1.100:8080`（把 `192.168.1.100` 换成你的服务器 IP；默认绑 `0.0.0.0`，可远程直接访问）。

### 选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `--port <n>` | 8080 | 监听端口 |
| `--bind <addr>` | 0.0.0.0 | 绑定地址；改 `127.0.0.1` 仅本机可访问（配合 SSH 隧道做安全加固） |
| `--webroot <dir>` | `<exe>/../src/web` | 静态前端根目录 |
| `--allow-write` | 关 | 开 POST /api/inject\|clean（默认只读：GET 200，POST 403） |

### HTTP API

| 方法 | 路径 | 权限 | 说明 |
|---|---|---|---|
| GET | `/api/health` | 只读 | `{"writable": bool}` 探活 |
| GET | `/api/catalog` | 只读 | 故障目录 |
| GET | `/api/state` | 只读 | 活跃注入记录 |
| GET | `/api/history` | 只读 | 全部历史记录 |
| POST | `/api/inject` | `--allow-write` | body `{"uid":"...","params":{...}}` |
| POST | `/api/clean` | `--allow-write` | body `{"uid":"...","params":{...}}` |

> 所有响应带 `Cache-Control: no-store`；只读模式下 POST 返回 403。

### 安全模型

- **默认只读**：不开 `--allow-write` 时，POST /api/inject\|clean 返回 403，只能查看 + 复制命令到终端执行。
- **`--allow-write` opt-in**：显式开启才允许浏览器注入/清理（前端注入前 `confirm()` 二次确认）。
- **默认绑 0.0.0.0**：方便远程直接访问。

> **安全加固（可选）**：HTTP 是明文传输，无认证。如需加密 + 认证保护，用 `--bind 127.0.0.1` + SSH 隧道：
>
> **第 1 步 — 服务器上拉起 serve**（绑 127.0.0.1，只接受本机回环连接）：
> ```bash
> ./build/dcat serve --port 8080 --bind 127.0.0.1
> ```
>
> **第 2 步 — 你的电脑上开 SSH 隧道**（把本地 8080 端口通过 SSH 转发到服务器 8080）：
> ```bash
> ssh -L 8080:localhost:8080 user@192.168.1.100
> #         └─ 本地端口     └─ 服务器端口（要和 --port 一致）
> # 本地端口可随意换，比如 -L 6060:localhost:5050 → 浏览器开 http://localhost:6060
> # SSH 端口非 22 用 -p 指定，比如 ssh -L 8080:localhost:8080 -p 2125 user@192.168.1.100
> # 把 user 和 192.168.1.100 换成你的 SSH 用户名和服务器 IP
> ```
> 输入 SSH 密码后保持终端窗口开着（窗口关了隧道就断）。
>
> **第 3 步 — 浏览器访问**：
> ```
> http://localhost:8080
> ```
> 此时你电脑 → localhost:8080 的流量，经 SSH 加密隧道转发到服务器 localhost:8080，全程加密且经过 SSH 账号认证。
>
> **原理**：`--bind 127.0.0.1` 让 serve 只接受服务器本机连接，外部网络无法直接访问；SSH 隧道在两者间建了一条加密管道，把你的请求安全地送到 serve。

### 前端仪表盘

打开 `http://localhost:8080`：

- **连接状态**：顶栏连接 pill（已连接·可写/只读 / 未连接），3 秒轮询探活。
- **摘要卡**：活跃注入数 / 历史总数 / 已清理数 / 故障总数。
- **活跃注入**：当前系统上的活跃故障（含参数 + 命令预览 + 清理按钮）。
- **历史记录**：最近 5 条历史（含命令 + 状态 pill）。
- **故障目录**：全部故障（含简介），点「构造命令」打开命令构造器（参数中文提示 + 实时命令预览）。

## 当前故障目录（33 条）

### CPU 模块（2 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rCPU_overload` | cores | — | CPU核心满载（支持多核心同时满载，perl纯用户态） |
| `rCPU_core_offline` | cores | — | CPU 核离线（sysfs） |

### 存储模块（1 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rDISK_write_overload` | device | workers(默认4), size_mb(默认200) | 磁盘写压（dd 多实例） |

### 网络模块（11 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rNET_delay` | iface, delay_ms | — | 网络延迟（tc netem） |
| `rNET_loss` | iface, loss_pct | — | 网络丢包（tc netem） |
| `rNET_reorder` | iface, reorder_pct | — | 网络乱序（tc netem） |
| `rNET_down` | iface | — | 网卡 down（ip link） |
| `rNET_degrade` | iface | speed_mbps(默认10) | 网卡降速（tc tbf） |
| `rNET_port_occupy` | port | protocol(默认tcp) | 端口占用（socket holder） |
| `rNET_service_stop` | service | — | 服务停止（systemctl） |
| `rNET_link_flap` | iface | cycle_sec(默认2), count(默认10) | 链路闪断（ip link 循环） |
| `rNET_bw_limit` | iface, rate_kbps | — | 带宽限制（tc tbf） |
| `rNET_jitter` | iface, delay_ms, jitter_ms | — | 延迟抖动（tc netem） |
| `rNET_tcp_loss` | port | direction(默认both) | TCP 丢包（iptables DROP） |

### 进程模块（3 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rPROC_exit` | pid | — | 进程退出（kill -9，不可恢复，inject-only） |
| `rPROC_hang` | pid | — | 进程挂起（SIGSTOP） |
| `rPROC_zstate` | pid | — | 僵尸进程（kill 目标进程 → 僵尸，clean 杀父进程回收，不可恢复） |

### NPU 模块（16 条）

| UID | 必填 | 可选 | 说明 |
|---|---|---|---|
| `rNPU_link_down` | chip | — | RoCE 链路 down（-cfg recovery） |
| `rNPU_ip_change` | chip, address, netmask | — | RoCE IP 变更（sidecar 回放） |
| `rNPU_gw_change` | chip, gateway | — | RoCE 网关变更（sidecar 回放） |
| `rNPU_netdetect_change` | chip, address | — | Netdetect IP 变更（sidecar 回放） |
| `rNPU_arp_poison` | chip, dev, ip, mac | — | ARP 毒化（add wrong mac） |
| `rNPU_arp_del` | chip, dev, ip | — | ARP 条目删除（sidecar 回放） |
| `rNPU_route_add` | chip, address, netmask, gateway | — | 添加 RoCE 路由（del 清理） |
| `rNPU_route_del` | chip, address, netmask | — | 删除 RoCE 路由（sidecar 回放） |
| `rNPU_iprule_add` | chip, dir, ip, table | — | 添加 ip rule（del 清理） |
| `rNPU_iprule_del` | chip, dir, ip | — | 删除 ip rule（sidecar 回放） |
| `rNPU_iproute_add` | chip, ip, ip_mask, via, dev, table | — | 添加 ip route（del 清理） |
| `rNPU_iproute_del` | chip, ip, ip_mask, table | — | 删除 ip route（sidecar 回放） |
| `rNPU_bw_limit` | chip, bw_limit | — | RoCE 带宽限速（设回 max） |
| `rNPU_mtu_mismatch` | chip, size | — | RoCE MTU 变更（sidecar 回放） |
| `rNPU_dscp_tc_change` | chip, dscp, tc | — | DSCP→TC 映射变更（sidecar 回放） |
| `rNPU_roce_port_change` | chip, port | — | RoCE UDP 端口变更（sidecar 回放） |

## 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 运行错误（脚本失败等） |
| 2 | 解析错误（命令格式不合法） |
| 3 | 预检拒绝（参数缺失/不合法/op 不支持） |
| 4 | 未找到（uid 不在目录中） |
| 5 | 重注入拒绝（同资源已注入，需 `--force`） |

## E2E 测试架构

E2E 测试采用 **CSV 驱动 + 8 类分类** 的混沌工程测试矩阵，用例由 `tests/e2e/gen_cases.py` 自动生成，`tests/e2e/run_e2e.py` 串行执行。

### 8 类分类

| 分类 | 前缀 | 覆盖内容 | 混沌工程维度 |
|---|---|---|---|
| **FUNC** | `FUNC-` | 33 故障 inject→verify→clean→query 全链路 + query\<uid\> confirmed + 插件 | 功能基线 |
| **BOUND** | `BOUND-` | 每参数类型系统性覆盖（整数越界/空值/格式错误/枚举非法） | 边界值 |
| **SEC** | `SEC-` | 命令注入(inject+clean+query) + 权限边界 + 主机安全 + symlink 攻击 | 安全 |
| **STATE** | `STATE-` | clean×2/--force/reinject 拒绝/query 幂等/并发 inject 同/不同资源 | 状态一致性 |
| **RES** | `RES-` | state 丢失/损坏/孤儿/幽灵/clean --all 幂等/state 表满 | 韧性/自愈 |
| **CLI** | `CLI-` | 解析错误 + 帮助 + 退出码 + --config + 未知 uid | CLI 接口 |
| **CONC** | `CONC-` | 同时 inject+clean / 双进程写 state / clean --all + inject | 并发竞争 |
| **INTER** | `INTER-` | 多故障叠加 / clean 一个不影响其他 / clean --all 后逐 verify | 故障交互 |

### 运行 E2E 测试

```bash
# 生成用例（354 步骤 / 165 流程）
python3 tests/e2e/gen_cases.py

# 执行（需要 root 权限以覆盖全部用例）
sudo python3 tests/e2e/run_e2e.py

# 只跑指定分类
sudo python3 tests/e2e/run_e2e.py --flows FUNC,BOUND,SEC
```

> **⚠️ NPU 测试参数需按实机拓扑调整**：`tests/e2e/gen_cases.py` 中 `rNPU_*` 故障的 `inject_args`/`clean_args`/`setup_cmd`/`v_cmd` 采用**示例拓扑值**（如 `--chip=2`、网关 `10.30.12.254`、接口 `eth2`、测试子网 `10.30.40.0/10.30.50.0` 等）。这些值是**机器相关**的：不同 NPU 物理机的 IP/网关/接口名/静态 ARP/ip_rule 可能不同，直接照搬会在其他机器上 FAIL。部署到新机器前，请先用 `hccn_tool` 查询实际参数（`-ip -g`/`-gateway -g`/`-route -g`/`-arp -g`/`-ip_rule -g`/`-ip_route -g table <n>` 及网口名 `-status -g`）并同步更新 `gen_cases.py`；`run_e2e.py` 的 atexit NPU 清理地址也需对应修改。详见 [docs/user_manual.md 第五章](docs/user_manual.md#第五章-npu-模块16-条)。

### 用例统计

| 分类 | 步骤数 | 流程数 | 说明 |
|---|:---:|:---:|---|
| FUNC | 150 | 41 | 33 故障 inject→verify→clean→query 全链路 + query\<uid\> confirmed + 插件 |
| BOUND | 49 | 46 | 每参数类型系统性覆盖（整数越界/空值/格式错误/枚举非法） |
| SEC | 50 | 37 | 命令注入 + 权限边界 + 主机安全 + symlink 攻击 |
| STATE | 27 | 7 | clean×2/--force/reinject 拒绝/query 幂等/并发 inject |
| RES | 27 | 7 | state 丢失/损坏/孤儿/幽灵/clean --all/state 表满 |
| CLI | 28 | 21 | 解析错误 + 帮助 + 退出码 + --config + serve |
| CONC | 9 | 3 | 同时 inject+clean / 双进程写 state / clean --all + inject |
| INTER | 14 | 3 | 多故障叠加 / clean 一个不影响其他 / clean --all 后逐 verify |
| **总计** | **354** | **165** | |

详细测试设计见 [DESIGN.md §10](DESIGN.md) 和 [tests/e2e/README.md](tests/e2e/README.md)。

## 技术栈

- C11（ISO/IEC 9899:2011），CMake 构建
- cJSON（vendored 单文件库）
- pthread（状态锁）
- INI 配置文件（`demoncat.conf`）
- 输出格式：JSON（`list` 为可读文本表格）
