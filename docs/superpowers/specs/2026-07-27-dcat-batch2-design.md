# DemonCAT 批次 2 设计规格 — 28 条新故障

> 日期: 2026-07-27
> 范围: 在 v0.1 的 37 条基础上新增 28 条故障,引入 4 个新模块(memory / filesystem / docker / system),分 3 相交付。
> 注: 设计阶段为 30 条,实现后 `rFS_fd_exhaust` 与 `rFS_deadlock` 两条删除(语义不佳),实际交付 28 条;filesystem 模块保留 2 条。
> 架构遵循 SPEC.md: **外部脚本 + 声明式 cnf,免重新编译**,模块为 cnf 字符串字段,新模块无需改核心。

---

## 1. 歧义决策(用户授权自主裁定,供复核)

用户离席前确认 "3 相分批" + "核挂死按系统 CPU 核 id(类似核离线参数形态)"。其余歧义项按下方专家判断定下:

| # | 歧义项 | 决策 | 理由 |
|---|---|---|---|
| 1 | CPU 核挂死 语义 | 用 `chrt -f 99`(SCHED_FIFO 实时优先级)+ `taskset -c <core>` 钉一个死循环进程独占该核,使该核对普通任务"挂死"(被 RT 进程饿死)。区别于 rCPU_overload(纯 burn,非 RT)和 rCPU_core_offline(sysfs 下线) | 无内核模块即可实现;参数形态同核离线(`cores`);clean 杀 RT 进程即恢复 |
| 2 | 文件不可读/写/读写/删/重命名 粒度 | 单条 `rFS_file_lock`,带 `mode` 参数:`noread`(chmod 000)/`nowrite`(去写位)/`norw`(chmod 000)/`nodelete`(`chattr +i` 不可变,同时禁止删除与重命名) | 用户列的"不可删除"与"不可重命名"在 Linux 同由 immutable 属性实现,合并为一个 mode;clean 恢复原 mode/attr(存 sidecar) |
| 3 | 死锁 语义 | 文件锁死锁:两个进程以相反顺序 `flock` 两把锁 → 互锁(D 态)。`rFS_deadlock`,可恢复(clean 杀两进程) | 可注入、可观测、无内核依赖;"死锁"最经典可注入形态 |
| 4 | iowait高 语义 | 多 worker 跑小块 `dd ... conv=fdatasync` 同步写,推高 iowait%。`rFS_iowait_high`,query 报 mpstat iowait。区别于 rDISK_write_overload(填带宽,大块) | 机制不同(同步小块 vs 大块灌满);观测指标不同(iowait% vs 写吞吐) |
| 5 | 进程句柄耗尽 vs 系统文件句柄耗尽 | `rFS_fd_exhaust` = 系统级 `fs.file-max` 限到小值(sysctl);`rPROC_fd_exhaust` = 单进程打开 fd 到 RLIMIT_NOFILE 上限 | 前者整机级(sysctl),后者进程级(ulimit/进程内 open);两者机制与影响域不同,保留为两条 |
| 6 | 系统内存不足 vs 内存泄漏 | `rMEM_leak` = 泄漏指定 `size_mb` 并持有;`rMEM_oom` = 无上限分配直至 OOM killer 触发 | 有界 vs 无界;前者可量化,后者触发事件 |
| 7 | swap 内存过载 | `rMEM_swap_overload` = 分配超过空闲 RAM 的 `size_mb` 并 dirty,强制换出至 swap | 与 oom 区别:有界、定向压 swap |
| 8 | NPU 降频(ipmitool) | `rNPU_freq_down`:优先 `npu-smi info -t freq -i <chip>` 设频;不可用则 `ipmitool` 做 BMC 级 power-cap 降频。参数 `chip` + `freq`/`ratio`。需 NPU+管理权限,脚本带能力探测与降级 | 用户明指 ipmitool;真实 NPU 频率由 npu-smi/hccn_tool 管,ipmitool 走 BMC 兜底。P3 需硬件验证 |
| 9 | aic/aiv/HBM NPU 故障 | 拆 3 条:`rNPU_aic_fault`(AI 核)/`rNPU_aiv_fault`(AI 向量核)/`rNPU_hbm_fault`(高带宽内存)。均 `chip` 参数,调 `npu-smi` 子命令注入对应子单元故障,clean 走 `npu-smi -cfg recovery` 兜底 | 具体子命令需在 Atlas 硬件上确认;脚本写好结构 + 能力探测,P3 现场标定 |
| 10 | 磁盘 io 延迟/错误 机制 | `rDISK_io_delay` = device-mapper `delay` 目标;`rDISK_io_error` = device-mapper `error` 目标。`rDISK_scsi_error` = SCSI 级 `fail_function`/debugfs(`make-it-fail`),需 CONFIG_FAIL_MAKE_REQUEST,能力探测降级 | dm-delay/dm-error 可移植;SCSI 注错是更底层的独立机制,三者保持区分 |
| 11 | 磁盘丢失 恢复 | `rDISK_loss`:`echo 1 > /sys/block/<dev>/device/delete` 摘盘;clean 走 `echo "- - -" > /sys/class/scsi_host/hostN/scan` 重扫。可恢复但需 rescan | 标准 SCSI 摘/扫机制 |
| 12 | 系统panic / 机器下电 恢复性 | 均仅 `inject`:`rSYS_panic` 用 `echo c > /proc/sysrq-trigger`(需 sysrq);`rSYS_poweroff` 用 `poweroff`。不可 clean,机器重启/上电才恢复 | 整机级不可逆,符合 inject-only 约定,入 manual_test_guide |
| 13 | 杀容器实例 恢复性 | `rDOCKER_kill`:`docker kill <container>`;clean `docker start <container>`。可恢复(容器可重启) | docker kill 是 stop(可重启),非 rm |

---

## 2. 故障总表(28 条)

字段:UID | module | supported_ops | inject_required | inject_optional | 一句话机制

### Phase 1 — 可恢复 + 新模块(18 条)

| UID | module | supported_ops | inject_required | inject_optional | 机制 |
|---|---|---|---|---|---|
| `rMEM_leak` | memory | inject,clean,query | size_mb | — | 后台进程 malloc 并持有 size_mb,不释放;clean 杀进程 |
| `rMEM_oom` | memory | inject,clean,query | — | rate_mb | 后台进程无上限分配直至 OOM killer;clean 杀进程 |
| `rMEM_fragment` | memory | inject,clean,query | — | blocks,block_kb | 后台进程分配 N 块、隔块释放造碎片;clean 杀进程 |
| `rMEM_swap_overload` | memory | inject,clean,query | size_mb | — | 分配 size_mb(>空闲 RAM)并 dirty 强制 swap;clean 杀进程 |
| `rCPU_quota` | cpu | inject,clean,query | quota_pct | cg_path | cgroup v1/v2 设 cpu 配额为 quota_pct%(1-99);clean 还原 |
| `rCPU_freq` | cpu | inject,clean,query | cores,freq_mhz | — | cpufreq sysfs 设 scaling_max/min_freq;clean 还原(存 sidecar) |
| `rDISK_part_full` | storage | inject,clean,query | path | size_mb | 在 path 灌大文件至满/size_mb;clean 删文件 |
| `rDISK_inode_exhaust` | storage | inject,clean,query | path | count | 在 path 建 N 个空文件耗尽 inode;clean rm |
| `rDISK_io_delay` | storage | inject,clean,query | device,delay_ms | — | device-mapper delay 目标;clean 卸载还原 |
| `rDISK_io_error` | storage | inject,clean,query | device | — | device-mapper error 目标;clean 还原 |
| `rNET_corrupt` | network | inject,clean,query | iface,corrupt_pct | — | tc netem corrupt;clean 删 qdisc |
| `rNET_conn_exhaust` | network | inject,clean,query | target | count | 后台进程持有 N 条 TCP 出向连接;clean 杀进程 |
| `rPROC_fork_bomb` | process | inject,clean,query | count | — | fork N 个 sleep 子进程;clean 杀进程组 |
| `rPROC_loop` | process | inject,clean,query | — | threads | 后台进程(可选多线程)死循环;clean 杀进程 |
| `rFS_fd_exhaust` | filesystem | inject,clean,query | limit | — | sysctl fs.file-max=limit;clean 还原(存 sidecar) |
| `rFS_file_lock` | filesystem | inject,clean,query | path,mode | — | chmod 000/去写位/chattr +i;clean 还原(存 sidecar) |
| `rDOCKER_kill` | docker | inject,clean,query | container | — | docker kill;clean docker start |
| `rDOCKER_mem_overload` | docker | inject,clean,query | container,size_mb | — | docker exec 内分配 size_mb 触发容器 OOM;clean 杀 exec 进程 |

### Phase 2 — 高危/不可恢复(8 条)

| UID | module | supported_ops | inject_required | inject_optional | 机制 | 恢复 |
|---|---|---|---|---|---|---|
| `rSYS_panic` | system | inject | — | — | `echo c > /proc/sysrq-trigger` 触发 panic | 重启 |
| `rSYS_poweroff` | system | inject | — | — | `poweroff` | 物理上电 |
| `rCPU_core_hang` | cpu | inject,clean,query | cores | — | taskset+chrt -f 99 死循环独占核 | clean 杀 RT 进程 |
| `rDISK_scsi_error` | storage | inject,clean,query | device | — | fail_function make-it-fail | clean 关闭注入 |
| `rDISK_loss` | storage | inject,clean,query | device | — | echo 1 > /sys/block/X/device/delete | clean scsi rescan |
| `rFS_deadlock` | filesystem | inject,clean,query | — | — | 双进程反序 flock 互锁 | clean 杀两进程 |
| `rFS_iowait_high` | filesystem | inject,clean,query | — | workers | 多 worker 小块同步 dd 推高 iowait | clean 杀进程 |
| `rPROC_fd_exhaust` | process | inject,clean,query | — | count | 单进程 open fd 至 RLIMIT_NOFILE 上限 | clean 杀进程 |

> P2 中 `rCPU_core_hang`/`rDISK_scsi_error`/`rDISK_loss`/`rFS_deadlock`/`rFS_iowait_high`/`rPROC_fd_exhaust` 虽可恢复但仍高危或对环境有扰动,入 `manual_test_guide.md`。

### Phase 3 — NPU 高级(4 条)

| UID | module | supported_ops | inject_required | inject_optional | 机制 | 恢复 |
|---|---|---|---|---|---|---|
| `rNPU_freq_down` | npu | inject,clean,query | chip,freq | — | npu-smi 设频(降级 ipmitool power-cap) | clean 还原最大频率 |
| `rNPU_aic_fault` | npu | inject,clean,query | chip | — | npu-smi 注入 AI 核故障 | clean npu-smi -cfg recovery |
| `rNPU_aiv_fault` | npu | inject,clean,query | chip | — | npu-smi 注入 AI 向量核故障 | clean npu-smi -cfg recovery |
| `rNPU_hbm_fault` | npu | inject,clean,query | chip | — | npu-smi 注入 HBM 故障 | clean npu-smi -cfg recovery |

> P3 全部未在真实环境测试;脚本写好结构 + 能力探测 + 降级,需在 Atlas NPU 硬件上验证。

---

## 3. 实现约定

### 3.1 脚本契约(沿用 SPEC §5)
- 环境变量:`DCAT_OP`(inject/clean/query)、`DCAT_UID`、`DCAT_PARAM_<KEY>`
- 退出码:0 成功;非 0 失败
- pidfile:`/tmp/dcat-<uid>-<key>.pid`(长驻故障 spawn 子进程后写)
- sidecar:`/tmp/dcat-<uid>-<key>.sidecar`(存恢复所需元数据,如原 sysctl 值/原 chmod)
- query:输出系统证据 + 退出码 0=生效/1=未生效

### 3.2 模块目录
- 新建:`src/scripts/memory/`、`src/scripts/filesystem/`、`src/scripts/docker/`、`src/scripts/system/`
- 扩展:`src/scripts/cpu/`、`src/scripts/storage/`、`src/scripts/network/`、`src/scripts/process/`、`src/scripts/npu/`

### 3.3 配置(demoncat.conf)
每条加一段 `[fault.<uid>]`,字段同现有约定。新模块 `module` 取新值。

### 3.4 测试(TDD,SPEC §9)
- 新建表驱动测试文件:`test_faults_memory.c`、`test_faults_filesystem.c`、`test_faults_docker.c`、`test_faults_system.c`
- 扩展:`test_faults_cpu_storage.c`(加 rCPU_quota/rCPU_freq/rCPU_core_hang + 4 storage)、`test_faults_network.c`(加 2)、`test_faults_process.c`(加 3)、`test_faults_npu.c`(加 4)
- 模式:`mkparams` → `dispatch_route(uid,op,&p)` → `CMD_CONTAINS("脚本名")` + `check_param_env` + clean;inject-only 额外断言 `record_id==NULL` 且 clean/query 返回码 3
- 新测试文件在 CMakeLists 用 `dcat_test()` 注册
- 所有新脚本必须过 `sh -n`(check_syntax.sh 自动覆盖 `src/scripts/*/*.sh`)

### 3.5 文档
- `docs/user_manual.md`:加第六/七/八/九/十章(memory/filesystem/docker/system)+ 各章小节,TOC 同步(沿用刚做的 header+TOC+锚点同步法)
- `README.md`:故障目录表加 28 条,合计改 65

### 3.6 验证
- `cmake -B build && cmake --build build`
- `ctest --test-dir build --output-on-failure` 全绿
- `sh tests/check_syntax.sh` 全绿

---

## 4. 交付顺序
P1(18) 全实现+测试+文档+构建绿 → P2(8) → P3(4)。每相结束 ctest 全绿。
