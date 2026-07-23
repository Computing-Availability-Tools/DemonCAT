# DemonCAT 用户手册 (dcat)

> DemonCAT（`dcat`）—— Linux 计算故障注入 CLI。
> 覆盖 CPU / 存储 / 网络 / 进程模块。
> 完整规格见 [SPEC.md](SPEC.md)，架构见 [DESIGN.md](DESIGN.md)。

---

## 1. 编译与安装

```bash
cd /path/to/CAT
cmake -B build && cmake --build build

# 运行单元测试
ctest --test-dir build --output-on-failure
```

编译产物 `build/dcat` 即可使用，无需安装。配置文件默认在 `config/demoncat.conf`。

---

## 2. 命令格式

```
dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]
```

### 全局选项

| 选项 | 说明 |
|---|---|
| `--config <path>` | 指定配置文件路径（默认 `<binary_dir>/../config/demoncat.conf`） |
| `--help` | 显示帮助 |

### 子命令

| 子命令 | 说明 | uid 可省 | 适用故障 |
|---|---|---|---|
| `inject` | 注入故障，同步阻塞执行 | 否 | 所有 |
| `clean` | 按参数匹配清除活跃注入 | 否 | `inject,clean,query` |
| `query` | 查询活跃记录 / 验证故障生效 | 是 | `inject,clean,query` |
| `list` | 列出故障目录 | 是 | 所有 |

---

## 3. inject — 注入故障

### 基本用法

```bash
dcat inject <uid> --key=value [--key2=value2 ...]
```

dcat 在注入前做 4 步预检：uid 存在 → op 支持 → 必填参数齐全 → 脚本可执行。任一失败返回退出码 3 或 4。未声明的参数也会被拒绝（退出码 3）。

### 示例

#### CPU 过载

```bash
# 注入 4 核 CPU 满载
dcat inject rCPU_overload --cores=4
```

#### CPU 核离线

```bash
# 离线 CPU 2 和 4
dcat inject rCPU_core_offline --cores=2,4

# 离线 CPU 0-3
dcat inject rCPU_core_offline --cores=0-3

# 恢复
dcat clean rCPU_core_offline --cores=2,4
```

需要 root 权限。cpu0 通常不可离线，脚本自动跳过并提示。

#### 磁盘写压

```bash
# 注入磁盘写压（默认 4 个 dd worker，每个写 200MB）
dcat inject rDISK_write_overload --device=/tmp

# 自定义 worker 数和单次写入量
dcat inject rDISK_write_overload --device=/tmp --workers=8 --size_mb=500
```

#### 网络丢包

```bash
dcat inject rNET_loss --iface=eth0 --loss_pct=5
```

#### 进程退出（一次性，不可恢复）

```bash
dcat inject rPROC_exit --pid=12345
# 返回（无 record_id，不建 state）：
# {"status":"ok","op":"inject","uid":"rPROC_exit","data":{"message":"killed pid=12345"}}
```

### 并发注入

同 uid 不同参数允许并发：

```bash
dcat inject rCPU_overload --cores=2    # record_id=1
dcat inject rCPU_overload --cores=4    # record_id=2
dcat query                             # 两条活跃记录
dcat clean rCPU_overload --cores=2     # 只清 cores=2 的
```

---

## 4. clean — 清除故障

### 基本用法

```bash
dcat clean <uid> [--key=value ...]
```

dcat 按用户提供的参数匹配活跃记录，逐条执行 clean 脚本（传记录存储的 inject 参数）。某条失败时停止，剩余不清理。

### 示例

```bash
# 清除 CPU 过载（只清 cores=4 的记录）
dcat clean rCPU_overload --cores=4

# 清除磁盘写压
dcat clean rDISK_write_overload --device=/tmp

# 清除网络丢包
dcat clean rNET_loss --iface=eth0

# 清除该 uid 的所有活跃记录（不传参数）
dcat clean rCPU_overload
```

### inject-only 故障不支持 clean

```bash
dcat clean rPROC_exit --pid=12345
# {"status":"error","op":"clean","uid":"rPROC_exit","error":{"code":3,"message":"op not supported"}}
# 退出码 3
```

---

## 5. query — 查询

### 无 uid：查全部活跃记录

```bash
dcat query
# {"status":"ok","op":"query","data":[{"record_id":1,"uid":"rCPU_overload","started_at":...,"active":true,"params":{"cores":"4"}}]}
```

由 dcat 自身 state 回答，不调用脚本。

### 有 uid：验证故障是否真的生效

```bash
dcat query rCPU_overload --cores=4
# 脚本输出（原始）：
# requested_cores: 4
# yes_processes: 4
# --- cpu usage ---
# %Cpu(s): 98.0 us, 1.0 sy, 0.0 ni, 0.0 id
# ---
# {"status":"ok","op":"query","uid":"rCPU_overload","data":{"confirmed":true}}
```

调脚本 `DCAT_OP=query` 分支，检查实际系统状态。退出码 0 = 确认生效。

---

## 6. list — 故障目录

```bash
dcat list
# {"status":"ok","op":"list","data":[{"uid":"rCPU_overload","module":"cpu","supported_ops":["inject","clean","query"],"desc":"CPU overload (multi-core burn)","required_params":["cores"]},...]}
```

---

## 7. 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 运行错误（脚本非 0 退出、fork/exec 失败等） |
| 2 | 解析错误（命令格式不合法） |
| 3 | 预检拒绝（op 不支持、必填参数缺失、未声明参数、脚本不可执行） |
| 4 | 未找到（uid 不在目录中） |

---

## 8. 典型工作流

### 可恢复故障（inject → query → clean）

```bash
# 1. 注入
dcat inject rCPU_overload --cores=4

# 2. 验证生效
dcat query rCPU_overload --cores=4

# 3. 查看活跃记录
dcat query

# 4. 清除
dcat clean rCPU_overload --cores=4

# 5. 确认已清除
dcat query    # 空列表
```

### 一次性故障（inject → 终结）

```bash
dcat inject rPROC_exit --pid=12345
dcat query    # 看不到记录（inject-only 不建 state）
```

---

## 9. 扩展：新增故障

1. 编写脚本 `config/scripts/<module>/<fault_name>.sh`，实现 inject/clean/query 分支
2. 在 `config/demoncat.conf` 添加 `[fault.<uid>]` 段
3. 无需重新编译 — `dcat list` 自动出现新故障

脚本契约：
- 环境变量传参：`DCAT_OP`、`DCAT_UID`、`DCAT_PARAM_<KEY>`
- 退出码 `0` = 成功；非 `0` = 失败
- stdout 成功文本 → JSON `data.message`
- inject 需要长驻的故障：spawn 子进程 + 写 pidfile 后立即返回
- clean 读取 pidfile 清理资源
