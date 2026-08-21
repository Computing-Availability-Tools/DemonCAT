# DemonCAT 用户需求故事

> **画像**：验证系统韧性的测试工程师
> **范围**：按模块挑代表性/最易踩坑故障 + 跨切框架契约，共 18 个故事
> **依据**：[SPEC.md](../SPEC.md)、[User_Manual.md](../User_Manual.md)、[DESIGN.md](DESIGN.md)
> **格式**：Mike Cohn 用例（As a / I want to / so that）+ Gherkin 验收标准（Given/When/Then）
>
> 故障遵循同一 `inject/clean/query` 模式，故跨切框架契约（预检4步、JSON输出、退出码、错误隔离、开闭扩展、同步阻塞、日志、可测性）织入各故事验收标准中，并在 US-10~US-18 单列。

---

## 目录

- [US-01 rCPU_overload — 核满载与核集语义重注入防护](#us-01-rcpu_overload--核满载与核集语义重注入防护)
- [US-02 rDISK_write_overload — 磁盘写压与可选参数缺省默认](#us-02-rdisk_write_overload--磁盘写压与可选参数缺省默认)
- [US-03 rNET_loss — 网卡丢包与同网卡 qdisc 互斥](#us-03-rnet_loss--网卡丢包与同网卡-qdisc-互斥)
- [US-04 rNET_tcp_loss — 端口级 TCP 丢包与 direction 双向原子性](#us-04-rnet_tcp_loss--端口级-tcp-丢包与-direction-双向原子性)
- [US-05 rPROC_exit — 进程强杀（inject-only 契约）](#us-05-rproc_exit--进程强杀inject-only-契约)
- [US-06 rPROC_zstate — 僵尸进程与父进程回收竞争](#us-06-rproc_zstate--僵尸进程与父进程回收竞争)
- [US-07 rNPU_arp_poison — NPU ARP 毒化（机器相关参数）](#us-07-rnpu_arp_poison--npu-arp-毒化机器相关参数)
- [US-08 rNPU_arp_del — NPU ARP 删除（依赖前提条件）](#us-08-rnpu_arp_del--npu-arp-删除依赖前提条件)
- [US-09 rNPU_gw_change — RoCE 网关变更与网段/no-op 风险](#us-09-rnpu_gw_change--roce-网关变更与网段no-op-风险)
- [US-10 query（无 uid）— state 查询全部活跃记录](#us-10-query无-uid--state-查询全部活跃记录)
- [US-11 开闭扩展 — 加脚本+配置段免重新编译](#us-11-开闭扩展--加脚本配置段免重新编译)
- [US-12 错误隔离 — 单故障失败不影响主流程与其他故障](#us-12-错误隔离--单故障失败不影响主流程与其他故障)
- [US-13 配置自动定位 — /proc/self/exe 推导 + --config/--help](#us-13-配置自动定位--procselfexe-推导---config--help)
- [US-14 预检 4 步 — 校验链与退出码映射](#us-14-预检-4-步--校验链与退出码映射)
- [US-15 list — 列出故障目录](#us-15-list--列出故障目录)
- [US-16 同步阻塞执行与长驻故障脚本自管理](#us-16-同步阻塞执行与长驻故障脚本自管理)
- [US-17 日志级别控制](#us-17-日志级别控制)
- [US-18 mock_executor 表驱动断言](#us-18-mock_executor-表驱动断言)

---

## US-01 rCPU_overload — 核满载与核集语义重注入防护

- **Summary**: 注入指定核纯用户态满载以验证算力隔离韧性，靠核集交集语义防止重叠抢核

### Use Case:
- **As a** 验证 CPU 隔离与调度韧性的测试工程师
- **I want to** 向指定 CPU 核注入纯用户态 100% 满载（perl 优先、无 perl 回退 yes），对重叠核集重注入默认拒绝、需 `--force` 原子替换，并支持无参 query 查全部在线核
- **so that** 我能可重复地制造可控算力抢占场景，同时避免两次满载抢同一核导致故障叠加失真

### Acceptance Criteria:

- **Scenario**: 不同核段并发注入互不影响
- **Given**: rCPU_overload 已在 demoncat.conf 声明 `supported_ops=inject,clean,query`、`inject_required=cores`
- **and Given**: 系统已安装 taskset，perl 可用（或回退 yes）
- **When**: 测试工程师依次执行 `dcat inject rCPU_overload --cores=0,1` 与 `dcat inject rCPU_overload --cores=2,3`
- **Then**: 两次均返回 `status:ok` 与递增 `record_id`，核 0,1 与核 2,3 上各出现独立 burn 进程，互不重叠

- **Scenario**: 同核/重叠核集重注入默认拒绝
- **Given**: `dcat inject rCPU_overload --cores=0,1` 已成功注入（state 有活跃记录，资源键=cores 走核集交集）
- **When**: 测试工程师执行 `dcat inject rCPU_overload --cores=0-8`（核集与 0,1 相交）
- **Then**: 返回 `status:error`、退出码 5，message 列出重叠记录 id 与参数（最多 3 条，超出 `+N more`），不执行注入、无新 burn 进程

- **Scenario**: `--force` 原子替换先清后注
- **Given**: rCPU_overload 已注入 `--cores=0,1`（活跃记录存在）
- **When**: 测试工程师执行 `dcat inject rCPU_overload --cores=0-8 --force`
- **Then**: dcat 先逐个 clean 所有重叠记录（kill 旧 burn 进程、删 pidfile），全成功后再 inject 0-8，返回 `status:ok` 与新 `record_id`；clean 中途失败则中止，已清记录保持已清理、message 带出失败 record id

- **Scenario**: query 不强制必填，无参查全部在线核
- **Given**: rCPU_overload 已注入 `--cores=0,1`
- **and Given**: precheck 对 query 不做必填校验，仅校验参数已声明
- **When**: 测试工程师执行 `dcat query rCPU_overload`（不带 `--cores`）
- **Then**: 脚本读 `/sys/devices/system/cpu/online` 展示全部在线核占用与匹配 perl/yes 进程详情，进程数>0 时输出 `confirmed:true`、退出码 0

- **Scenario**: clean 时 pidfile 不存在报错 exit 1
- **Given**: rCPU_overload 的 pidfile `/tmp/dcat-rCPU_overload-<spec>.pid` 已丢失或被删
- **When**: 测试工程师执行 `dcat clean rCPU_overload --cores=0,1`
- **Then**: 脚本读不到 pidfile，报错并 `exit 1`，dcat 返回 `status:error`、退出码 1；需改走 `dcat clean rCPU_overload`（无参 stateless）或手动清理残留 burn 进程

- **Scenario**: clean 须传与 inject 完全相同 cores 规格
- **Given**: 已以 `--cores=0,1` 注入，pidfile 按原始参数串命名
- **When**: 测试工程师执行 `dcat clean rCPU_overload --cores=0-1`（规格串与 inject 不同）
- **Then**: pidfile 名不匹配，clean 找不到对应记录；clean 必须传入与 inject 完全相同的 cores 规格（如 `0,1`）才能命中

- **Scenario**: 无 perl 时回退 yes 仍可注入
- **Given**: 系统未安装 perl
- **When**: 测试工程师执行 `dcat inject rCPU_overload --cores=0,1`
- **Then**: 脚本回退为 `yes`（引入约 60% 系统调用开销），注入仍成功返回 `status:ok`；query 用 `pgrep -x yes` 统计 burn 进程

---

## US-02 rDISK_write_overload — 磁盘写压与可选参数缺省默认

- **Summary**: 多路 dd 写压目标盘以验证存储 IO 韧性，可选参数缺省走脚本默认值

### Use Case:
- **As a** 验证存储 IO 过载韧性的测试工程师
- **I want to** 向目标设备/目录注入多路 dd 并发写压（workers/size_mb 可选，缺省走默认），clean 按注入时的 device 匹配 pidfile 清理，无参时走 stateless 清该 uid 全部工件
- **so that** 我能可控地制造磁盘写 IO 饱和，且不因可选参数缺失而注入失败、不因 state.json 损坏而无法清理

### Acceptance Criteria:

- **Scenario**: 可选参数缺省走默认值成功注入
- **Given**: rDISK_write_overload 已声明 `inject_required=device`、`inject_optional=workers,size_mb`
- **When**: 测试工程师执行 `dcat inject rDISK_write_overload --device=/data`（不带 workers/size_mb）
- **Then**: 对应 `DCAT_PARAM_WORKERS`/`SIZE_MB` 环境变量未设置，脚本走默认 workers=4、size_mb=200，返回 `status:ok` 与 `record_id`，/data 下出现 `dcat.stress.*` 临时文件与 4 个 dd 进程

- **Scenario**: clean 按参数匹配活跃记录逐条清理
- **Given**: 已注入 `/data` 与 `/var` 两条记录（不同 device）
- **When**: 测试工程师执行 `dcat clean rDISK_write_overload --device=/data`
- **Then**: dcat 按 device 匹配 state 记录，仅清理 /data 对应 worker 进程与临时文件，/var 记录保持活跃；某条 clean 失败则停止、剩余不清理且不 mark inactive

- **Scenario**: 无参 clean 走 stateless 清该 uid 全部工件
- **Given**: /data 与 /var 两条记录存在
- **When**: 测试工程师执行 `dcat clean rDISK_write_overload`（无参）
- **Then**: 走 stateless 路径，脚本自行 glob `/tmp/dcat-rDISK_write_overload-*` 工件清理该 uid 全部注入，不查 state；state.json 丢失/损坏时仍可清

- **Scenario**: device 为目录与非目录的临时文件路径差异
- **Given**: rDISK_write_overload 已声明 `inject_required=device`
- **When**: 测试工程师分别以 `--device=/data`（目录）与 `--device=/dev/sdb1`（非目录设备）注入
- **Then**: 目录模式下在 /data 下写 `dcat.stress.*`；非目录模式写 `/tmp/dcat.write.*`；clean 按对应路径清理临时文件

---

## US-03 rNET_loss — 网卡丢包与同网卡 qdisc 互斥

- **Summary**: 注入网卡随机丢包以验证应用容错，同网卡已有 root qdisc 时注入失败提示先 clean

### Use Case:
- **As a** 验证网络丢包容错的测试工程师
- **I want to** 在指定网卡出向注入 tc netem 随机丢包，同网卡已有 root qdisc 时注入失败，同 iface 重注入默认拒绝
- **so that** 我能精确模拟单网卡丢包场景，且不因 qdisc 叠加或同资源重注入导致故障失真

### Acceptance Criteria:

- **Scenario**: 正常注入写 sidecar 与 state 记录
- **Given**: rNET_loss 已声明 `inject_required=iface,loss_pct`
- **and Given**: eth0 当前无 root qdisc，具备 CAP_NET_ADMIN
- **When**: 测试工程师执行 `dcat inject rNET_loss --iface=eth0 --loss_pct=5`
- **Then**: 执行 `tc qdisc add dev eth0 root netem loss random 5%`，sidecar `/tmp/dcat-rNET_loss-eth0.sidecar` 写入网卡名，返回 `status:ok` 与 `record_id`

- **Scenario**: 同网卡已注入 qdisc 故障时新注入失败
- **Given**: eth0 已注入 rNET_loss（存在 root netem qdisc）
- **When**: 测试工程师执行 `dcat inject rNET_jitter --iface=eth0 --delay_ms=100 --jitter_ms=20`
- **Then**: `tc qdisc add` 失败（已有 root qdisc），脚本非 0 退出，dcat 返回 `status:error`、退出码 1，不写 state；message 提示需先 `dcat clean` 或 `tc qdisc del dev eth0 root`

- **Scenario**: 同 iface 重注入默认拒绝，`--force` 替换
- **Given**: rNET_loss 已注入 `--iface=eth0`（活跃记录存在，资源键=iface 精确等值）
- **When**: 测试工程师执行 `dcat inject rNET_loss --iface=eth0 --loss_pct=8`
- **Then**: 返回 `status:error`、退出码 5（同资源重注入），不执行注入；改加 `--force` 则先 clean 旧记录再 inject 新值

- **Scenario**: `clean --all` stateless 清全部网卡
- **Given**: eth0 与 eth1 均已注入 rNET_loss
- **When**: 测试工程师执行 `dcat clean --all`
- **Then**: 对全部支持 clean 的故障 fan-out 无参 stateless clean，聚合返回 `{uid,status}` 数组；state.json 丢失/损坏时仍可清

---

## US-04 rNET_tcp_loss — 端口级 TCP 丢包与 direction 双向原子性

- **Summary**: 注入 iptables 端口级 TCP 丢包以验证连接韧性，direction=both 双向原子注入

### Use Case:
- **As a** 验证 TCP 连接韧性的测试工程师
- **I want to** 在指定端口注入 iptables DROP 规则（direction 默认 both），in/out/both 各方向规则独立、任一方向插入失败即整体报错，sidecar 后缀为 `.rule`
- **so that** 我能精确控制丢包方向并确保不留孤立规则

### Acceptance Criteria:

- **Scenario**: direction=both 双向注入原子性
- **Given**: rNET_tcp_loss 已声明 `inject_required=port`、`inject_optional=direction`（默认 both）
- **When**: 测试工程师执行 `dcat inject rNET_tcp_loss --port=8080`（direction 缺省 both）
- **Then**: 同时插入 INPUT(dport) 与 OUTPUT(sport) 两条 DROP 规则，sidecar 后缀为 `.rule`（非 `.sidecar`），返回 `status:ok`；任一方向插入失败则整体报错退出、不写 state

- **Scenario**: query 的 direction 需与 inject 一致才能匹配
- **Given**: 已以 `direction=in` 注入（仅 INPUT 规则）
- **When**: 测试工程师执行 `dcat query rNET_tcp_loss --port=8080 --direction=out`
- **Then**: 仅查 OUTPUT 链，`DROP.*spt:8080` 不匹配，输出 `confirmed:false`、退出码非 0（方向不一致导致误判）

- **Scenario**: clean 从 sidecar 读 port+dir 精确删规则
- **Given**: 已以 direction=both 注入，sidecar 含 port 与 dir
- **When**: 测试工程师执行 `dcat clean rNET_tcp_loss --port=8080`
- **Then**: 从 sidecar 回退 dir=both，对 INPUT/OUTPUT 各执行 `iptables -D ...` 删除规则并删 sidecar；规则已被手动删时 `-D` 静默失败不报错

---

## US-05 rPROC_exit — 进程强杀（inject-only 契约）

- **Summary**: 强杀目标进程以验证进程级故障韧性，inject-only 不支持 clean/query

### Use Case:
- **As a** 验证进程崩溃韧性的测试工程师
- **I want to** 用 `kill -9` 强制终止目标进程，该故障为 inject-only 不写 state、不支持 clean/query，可对同 pid 重复注入
- **so that** 我能模拟进程不可恢复退出，且不被误提供无法回滚的 clean 操作

### Acceptance Criteria:

- **Scenario**: inject 成功不写 state、不返回 record_id
- **Given**: rPROC_exit 已声明 `supported_ops=inject`、`inject_required=pid`
- **and Given**: 目标 pid 12345 存在且测试工程师有发信号权限
- **When**: 测试工程师执行 `dcat inject rPROC_exit --pid=12345`
- **Then**: 进程被 kill -9 终止，返回 `status:ok`、`data.message` 含 `killed pid 12345`，**不返回 `record_id`**、不写 state.json

- **Scenario**: clean/query 在 precheck 阶段被拒
- **Given**: rPROC_exit 的 supported_ops 仅 inject
- **When**: 测试工程师执行 `dcat clean rPROC_exit --pid=12345` 或 `dcat query rPROC_exit`
- **Then**: precheck 第 2 步判定 op 不属于 supported_ops，返回 `status:error`、退出码 3（预检拒绝），不调用脚本

- **Scenario**: 同 pid 可重复 inject（无 state 无重叠拒绝）
- **Given**: 已对 pid 12345 执行过 rPROC_exit（inject-only 无 state 记录）
- **When**: 测试工程师再次执行 `dcat inject rPROC_exit --pid=12345`
- **Then**: 天然不触发 reinject 拒绝（无活动记录），返回 `status:ok`；对已死 pid 的 kill 行为由脚本自行处理

- **Scenario**: 权限不足 kill 失败报错
- **Given**: 目标 pid 12345 属其他用户，测试工程师无发信号权限
- **When**: 测试工程师执行 `dcat inject rPROC_exit --pid=12345`
- **Then**: `kill -9` 失败（EPERM），脚本非 0 退出，dcat 返回 `status:error`、退出码 1，进程未被终止；需以 root 或具对应权限执行

---

## US-06 rPROC_zstate — 僵尸进程与父进程回收竞争

- **Summary**: 制造僵尸进程以验证父进程回收逻辑，clean 杀父进程强制 reparent 回收

### Use Case:
- **As a** 验证僵尸进程处理与回收逻辑的测试工程师
- **I want to** kill 目标进程使其在父进程未 wait 时残留为 Z 状态，clean 通过杀父进程 reparent 到 init 强制回收
- **so that** 我能可控地复现僵尸场景并验证系统回收行为

### Acceptance Criteria:

- **Scenario**: 父进程未回收时僵尸持续存在
- **Given**: rPROC_zstate 已声明 `inject_required=pid`，目标 pid 的父进程不主动 wait
- **When**: 测试工程师执行 `dcat inject rPROC_zstate --pid=12345`
- **Then**: 目标被 kill -9，sidecar 记录目标 pid 与 PPID，`/proc/12345/status` 的 State 为 Z，query 返回 `confirmed:true`

- **Scenario**: 父进程立即回收时僵尸不持续（正常行为非故障）
- **Given**: 目标 pid 的父进程会主动 wait 回收子进程
- **When**: 测试工程师执行 `dcat inject rPROC_zstate --pid=12345`
- **Then**: 目标被 kill 后父进程立即回收，僵尸不持续；query 返回 `confirmed:false`，此为正常现象非注入失败

- **Scenario**: clean 杀父进程强制 reparent 到 init 回收
- **Given**: 僵尸 12345 仍存在（sidecar 有 pid 与 PPID）
- **When**: 测试工程师执行 `dcat clean rPROC_zstate --pid=12345`
- **Then**: 若僵尸仍存在，kill 父进程使僵尸 reparent 到 init（PID 1）自动回收；clean 后目标与父进程均已终止，无法自动恢复，需手动重启

- **Scenario**: clean 时僵尸已被父进程回收则无需操作
- **Given**: inject 后僵尸已被父进程主动 wait 回收（`/proc/<pid>` 不存在或 State 非 Z）
- **When**: 测试工程师执行 `dcat clean rPROC_zstate --pid=12345`
- **Then**: 检测僵尸不存在，跳过杀父进程操作，返回 `status:ok`（幂等，不报错）

---

## US-07 rNPU_arp_poison — NPU ARP 毒化（机器相关参数）

- **Summary**: 向 NPU 注入伪造 ARP 表项以误导 RoCE 流量，dev 用 hccn_tool 查出的 NPU 内部网口名

### Use Case:
- **As a** 验证华为 Atlas NPU RoCE 流量韧性的测试工程师
- **I want to** 向指定芯片 RoCE 网口注入伪造 ARP（poison），dev 用 `hccn_tool` 查出的 NPU 内部网口名而非系统接口名，mac 为伪造地址
- **so that** 我能可控误导 RoCE 流量到错误 MAC，且不因照抄示例参数而注入失败

### Acceptance Criteria:

- **Scenario**: poison 注入伪造 ARP 并可 query 验证
- **Given**: 已用 `hccn_tool -i 2 -status -g` 查出目标芯片真实 dev（示例机 chip2=eth2，勿照抄）
- **and Given**: ip 选用 NPU 同网段未占用地址
- **When**: 测试工程师执行 `dcat inject rNPU_arp_poison --chip=2 --dev=eth2 --ip=10.30.12.200 --mac=00:11:22:33:44:55`
- **Then**: 执行 `hccn_tool -i 2 -arp -a dev eth2 ip ... mac ...`，返回 `status:ok`；query 检查 ARP 表同时存在该 ip 与 mac 时 `confirmed:true`

- **Scenario**: clean 按 ip+dev 删除伪造 ARP
- **Given**: 已注入 rNPU_arp_poison
- **When**: 测试工程师执行 `dcat clean rNPU_arp_poison --chip=2 --dev=eth2 --ip=10.30.12.200`
- **Then**: 执行 `hccn_tool -i 2 -arp -d dev eth2 ip ...` 删除该伪造 ARP 条目，返回 `status:ok`

> `clean --all` 对此类需 chip+标识参数的 NPU 故障报 `no active injection` 不实际清理，详见 [US-08](#us-08-rnpu_arp_del--npu-arp-删除依赖前提条件)。

---

## US-08 rNPU_arp_del — NPU ARP 删除（依赖前提条件）

- **Summary**: 删除指定芯片已有 ARP 表项以中断对应 IP 流量，clean 从 sidecar 恢复原 MAC

### Use Case:
- **As a** 验证华为 Atlas NPU RoCE 流量韧性的测试工程师
- **I want to** 删除指定芯片已有 ARP 表项（前提：该 ARP 必须已存在），clean 从 sidecar 取原 MAC 重新添加恢复
- **so that** 我能可控地制造 ARP 缺失导致的流量停滞，且不因删除不存在的条目而误判、不因 dev 配错而恢复失败

### Acceptance Criteria:

- **Scenario**: 前提满足时删除已存在 ARP 并 sidecar 存原 MAC
- **Given**: 目标 ARP 条目已存在（已用 `-arp -g` 确认或先 inject rNPU_arp_poison 创建）
- **and Given**: dev 用 `hccn_tool -i <chip> -status -g` 查出的 NPU 内部网口名（示例机 chip2=eth2，勿照抄）
- **When**: 测试工程师执行 `dcat inject rNPU_arp_del --chip=2 --dev=eth2 --ip=10.30.12.200`
- **Then**: 先 `-arp -g` 取原 MAC 存 sidecar，再 `-arp -d` 删除，返回 `status:ok`；query 检查该 ip 已不存在时 `confirmed:true`

- **Scenario**: 前提不满足（ARP 不存在）报错退出
- **Given**: 目标 ARP 条目未预先创建
- **When**: 测试工程师执行 `dcat inject rNPU_arp_del --chip=2 --dev=eth2 --ip=10.30.12.200`
- **Then**: `hccn_tool -arp -d` 报 `The configuration does not exist`，脚本非 0 退出，dcat 返回 `status:error`；需先 inject rNPU_arp_poison 创建再 del

- **Scenario**: clean 从 sidecar 恢复原 MAC，dev 须与创建时一致
- **Given**: 已注入 arp_del，sidecar 保存原 MAC
- **When**: 测试工程师执行 `dcat clean rNPU_arp_del --chip=2 --dev=eth2 --ip=10.30.12.200`（dev 与创建时一致）
- **Then**: 从 sidecar 取原 MAC 执行 `-arp -a` 恢复；dev 配错则 sidecar 匹配不到、恢复失败；sidecar 丢失则恢复为全零 MAC `00:00:00:00:00:00`

- **Scenario**: `clean --all` 对需 chip+标识参数的 NPU 故障报 no active injection 不实际清理
- **Given**: 此类 NPU 故障无 /tmp 工件可枚举其标识
- **When**: 测试工程师执行 `dcat clean --all`
- **Then**: 对 rNPU_arp_poison/del 等报 `no active injection`、退出 0、不实际清理，需带参 `clean <uid> --chip=N --key=...` 或依赖完好的 state.json

---

## US-09 rNPU_gw_change — RoCE 网关变更与网段/no-op 风险

- **Summary**: 修改 NPU RoCE 网关以验证跨网段路由韧性，网关须同网段且避免 no-op

### Use Case:
- **As a** 验证跨网段 RoCE 路由韧性的测试工程师
- **I want to** 修改指定芯片 RoCE 网关，gateway 必须与 NPU 当前 IP 同网段，原网关为 none 时 clean 跳过恢复
- **so that** 我能可控地制造跨网段路由失效，且不因网段不匹配或 no-op 导致注入无效

### Acceptance Criteria:

- **Scenario**: 网关同网段匹配成功注入
- **Given**: 已用 `hccn_tool -i 2 -ip -g` 查出 NPU IP=10.30.12.9/24，当前网关=10.30.12.254
- **When**: 测试工程师执行 `dcat inject rNPU_gw_change --chip=2 --gateway=10.30.12.1`（同网段且≠当前网关）
- **Then**: 先 `-gateway -g` 取原值 .254 存 sidecar，再 `-gateway -s gateway 10.30.12.1`，返回 `status:ok`；query 比对当前网关与原值不同则 `confirmed:true`

- **Scenario**: 网关与当前值相同导致 no-op 误判
- **Given**: 当前网关已是 10.30.12.254
- **When**: 测试工程师执行 `dcat inject rNPU_gw_change --chip=2 --gateway=10.30.12.254`（与当前相同）
- **Then**: `hccn_tool -gateway -s` 不触发变更，query 因状态未变误判 `confirmed:false`；注入前必须 `-gateway -g` 确认当前值再注入不同同网段地址

- **Scenario**: 网段不匹配被 hccn_tool 拒绝
- **Given**: NPU IP 在 10.30.12.0/24
- **When**: 测试工程师执行 `dcat inject rNPU_gw_change --chip=2 --gateway=192.168.1.1`（不同网段）
- **Then**: hccn_tool 报 `segment doesn't match`，脚本非 0 退出，dcat 返回 `status:error`

- **Scenario**: 原网关为 none 时 clean 跳过恢复
- **Given**: inject 时原网关为 none（未设网关），sidecar 存 none
- **When**: 测试工程师执行 `dcat clean rNPU_gw_change --chip=2`
- **Then**: clean 检测原值为 none 跳过恢复（不设回任何网关），返回 `status:ok`

---

## US-10 query（无 uid）— state 查询全部活跃记录

- **Summary**: 无 uid 查询由 dcat 自身 state 回答全部活跃注入，不调用脚本

### Use Case:
- **As a** 跟踪当前故障注入全貌的测试工程师
- **I want to** 不带 uid 执行 `dcat query`，由 dcat 遍历 state.json 活跃记录返回全部注入摘要，不调用任何脚本
- **so that** 我能快速盘点当前环境所有活跃故障，无需逐个调脚本验证

### Acceptance Criteria:

- **Scenario**: 无 uid 返回全部活跃记录数组
- **Given**: state.json 有多条活跃记录（rCPU_overload record_id=3、rNET_loss record_id=4）
- **When**: 测试工程师执行 `dcat query`（无 uid）
- **Then**: dcat 不调用脚本，遍历 state 返回 `{"status":"ok","op":"query","data":[{"uid":...,"record_id":3,"started_at":...,"active":true,"params":{...}},...]}`

- **Scenario**: query 无 uid 不受必填校验限制
- **Given**: 多条活跃记录存在
- **When**: 测试工程师执行 `dcat query`（无 uid 无参）
- **Then**: precheck 不做必填校验（query 无 uid 查全部不受参数限制），直接返回 state 摘要

- **Scenario**: 启动加载恢复 record_id 计数与未清理记录
- **Given**: dcat 上次退出后 state.json 存在未 clean 的活跃记录
- **When**: 重新启动 dcat 执行 `dcat query`
- **Then**: 启动加载 state.json 恢复 record_id 计数器与未清理记录，query 结果包含历史遗留的活跃记录

- **Scenario**: state.json 空或损坏时 query 无 uid 返回空数组
- **Given**: state.json 不存在、为空或损坏（无可解析活跃记录）
- **When**: 测试工程师执行 `dcat query`
- **Then**: 返回 `{"status":"ok","op":"query","data":[]}`（空数组），不崩溃；残留 /tmp 工件需用 `dcat clean --all`（stateless）清理

---

## US-11 开闭扩展 — 加脚本+配置段免重新编译

- **Summary**: 新增故障只需加可执行脚本+配置段，不动 dcat 二进制

### Use Case:
- **As a** 扩展故障目录的测试工程师
- **I want to** 在 `src/scripts/<module>/` 加可执行脚本 + `demoncat.conf` 加 `[fault.<uid>]` 段接入新故障，不修改 C 代码、不重新编译
- **so that** 我能快速扩充故障能力而不触发框架回归风险

### Acceptance Criteria:

- **Scenario**: 加脚本+配置段后 list 与 inject 立即生效
- **Given**: dcat 二进制已编译部署，未修改任何 C 代码
- **and Given**: 在 `src/scripts/network/` 加 `net_custom.sh`（可执行），`demoncat.conf` 加 `[fault.rNET_custom]` 段（module/script/supported_ops/inject_required 声明齐全）
- **When**: 测试工程师执行 `dcat list` 与 `dcat inject rNET_custom --iface=eth0`
- **Then**: `list` 输出含 rNET_custom 条目；inject 经 precheck 4 步通过后 fork/exec 脚本，经 `DCAT_OP`/`DCAT_UID`/`DCAT_PARAM_*` 环境变量传参，返回 `status:ok`

- **Scenario**: 脚本不可执行在 precheck 第 4 步被拒
- **Given**: `[fault.rNET_custom]` 的 script 指向无执行权限文件
- **When**: 测试工程师执行 `dcat inject rNET_custom --iface=eth0`
- **Then**: precheck 第 4 步 `access/X_OK` 失败，返回 `status:error`、退出码 3，不 fork 脚本

- **Scenario**: 参数走环境变量不走 argv，免 shell 注入
- **Given**: 新故障 inject_required 含可能被 shell 解释的值（如 `--msg=a;rm -rf /`）
- **When**: 测试工程师执行注入
- **Then**: 参数以 `DCAT_PARAM_MSG` 环境变量传递（非 argv），不触发 shell 注入；脚本按环境变量读取

---

## US-12 错误隔离 — 单故障失败不影响主流程与其他故障

- **Summary**: 单个故障 inject/clean 失败仅返回该次操作错误，不拖垮主流程与其他故障

### Use Case:
- **As a** 批量故障演练中关注稳定性的测试工程师
- **I want to** 单个故障脚本非 0 退出时 dcat 仅返回该次操作错误，不影响主流程与其他故障的独立操作
- **so that** 一个故障脚本崩溃不会拖垮整个演练批次

### Acceptance Criteria:

- **Scenario**: 单故障 inject 失败仅返回错误不崩溃
- **Given**: rNET_loss 脚本因缺 CAP_NET_ADMIN 非 0 退出
- **When**: 测试工程师执行 `dcat inject rNET_loss --iface=eth0 --loss_pct=5`
- **Then**: dcat 捕获脚本非 0 退出码，返回 `status:error`、退出码 1、`error.message` 含脚本 stderr，dcat 进程正常退出不崩溃

- **Scenario**: `clean --all` 中某 uid 失败不阻断其他 uid 聚合
- **Given**: rNET_loss 与 rCPU_overload 均已注入，rNET_loss clean 脚本失败
- **When**: 测试工程师执行 `dcat clean --all`
- **Then**: 聚合结果数组中 rNET_loss 标记失败、rCPU_overload 标记成功，失败的 uid 不阻断其他 uid 的 fan-out clean

- **Scenario**: inject-only 故障不写 state，query 无 uid 不含其记录
- **Given**: rPROC_exit 已注入（inject-only 无 state）
- **When**: 测试工程师执行 `dcat query`
- **Then**: query 结果不包含 rPROC_exit（无活跃记录），其他有 state 的活跃记录正常列出

---

## US-13 配置自动定位 — /proc/self/exe 推导 + --config/--help

- **Summary**: dcat 经 `/proc/self/exe` 推导固定相对配置路径，`--config` 可覆盖、`--help` 输出用法

### Use Case:
- **As a** 在不同部署路径下使用 dcat 的测试工程师
- **I want to** dcat 自动经 `/proc/self/exe` 解析自身路径推导 `<binary_dir>/../config/demoncat.conf`，无需环境变量；`--config` 可覆盖默认值，`--help` 输出统一命令用法
- **so that** 我能随二进制一同部署配置而无需设环境变量，测试场景可指定自定义配置、可随时查看用法

### Acceptance Criteria:

- **Scenario**: 默认推导二进制同级 config 目录
- **Given**: dcat 位于 `/opt/dcat/build/dcat`，配置部署在 `/opt/dcat/config/demoncat.conf`
- **When**: 测试工程师执行 `dcat list`（不带 `--config`）
- **Then**: dcat 经 `/proc/self/exe` 推导加载 `/opt/dcat/config/demoncat.conf`，列出其中声明的故障

- **Scenario**: `--config` 覆盖默认路径
- **Given**: 存在测试用配置 `/tmp/test-demoncat.conf`，仅含 rCPU_overload 一条故障
- **When**: 测试工程师执行 `dcat list --config /tmp/test-demoncat.conf`
- **Then**: 加载该自定义配置，`list` 仅输出 rCPU_overload 条目

- **Scenario**: 配置路径错误时优雅报错
- **Given**: `--config` 指向不存在的路径
- **When**: 测试工程师执行 `dcat list --config /no/such.conf`
- **Then**: dcat 报错退出（非崩溃），提示配置无法加载，不继续执行；具体退出码与文案未在 SPEC 显式定义，**需与需求方确认**

- **Scenario**: `--help` 全局选项输出用法
- **Given**: dcat 已部署
- **When**: 测试工程师执行 `dcat --help`
- **Then**: 输出统一命令格式 `dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]` 与 subcommand 集合（inject/clean/query/list）说明，退出码 0

---

## US-14 预检 4 步 — 校验链与退出码映射

- **Summary**: inject 请求按固定 4 步校验，任一失败即中止返回对应退出码

### Use Case:
- **As a** 验证预检护栏有效性的测试工程师
- **I want to** inject 请求按 uid 存在→op 合法→required 齐全→脚本可执行 4 步顺序校验，任一失败即中止返回错误 JSON 与对应退出码
- **so that** 非法请求在调用脚本前被拦截，避免无效注入副作用

### Acceptance Criteria:

- **Scenario**: uid 不存在返回退出码 4
- **Given**: `demoncat.conf` 未声明 rFAKE_fault
- **When**: 测试工程师执行 `dcat inject rFAKE_fault --x=1`
- **Then**: precheck 第 1 步失败，返回 `status:error`、退出码 4（未找到），不调用脚本

- **Scenario**: op 不在 supported_ops 返回退出码 3
- **Given**: rPROC_exit `supported_ops` 仅 inject
- **When**: 测试工程师执行 `dcat query rPROC_exit`
- **Then**: precheck 第 2 步失败，返回退出码 3（预检拒绝），不调用脚本

- **Scenario**: inject_required 缺失返回退出码 3
- **Given**: rCPU_overload `inject_required=cores`
- **When**: 测试工程师执行 `dcat inject rCPU_overload`（未提供 `--cores`）
- **Then**: precheck 第 3 步失败，返回 `status:error`、退出码 3，message 指出 `missing required param: cores`

- **Scenario**: 脚本不可执行返回退出码 3
- **Given**: `[fault.rNET_custom]` 的 script 指向无执行权限文件
- **When**: 测试工程师执行 `dcat inject rNET_custom --iface=eth0`
- **Then**: precheck 第 4 步 `access/X_OK` 失败，返回退出码 3，不 fork 脚本

- **Scenario**: clean/query 走第 1/2/4 步子集，query 不强制必填
- **Given**: rNET_loss `query_optional=iface,direction`（无 query_required 必填）
- **When**: 测试工程师执行 `dcat query rNET_loss`（无参）
- **Then**: precheck 跳过第 3 步必填校验（query 不强制必填），仅校验第 1/2/4 步与参数已声明，通过后调脚本展示全部

- **Scenario**: 未声明参数被拒退出码 3
- **Given**: rNET_loss 仅声明 `inject_required=iface,loss_pct`
- **When**: 测试工程师执行 `dcat inject rNET_loss --iface=eth0 --loss_pct=5 --foo=bar`
- **Then**: `--foo=bar` 未在 inject_required/optional 列表声明，返回退出码 3，不调用脚本

---

## US-15 list — 列出故障目录

- **Summary**: list 列出配置中声明的全部故障目录，输出 JSON 数组，不依赖 state

### Use Case:
- **As a** 发现可注入故障能力的测试工程师
- **I want to** 执行 `dcat list` 列出 demoncat.conf 声明的全部故障目录（uid/module/supported_ops/desc），不读 state、不调脚本
- **so that** 我能快速了解当前环境可注入哪些故障及其支持的操作，无需翻配置文件

### Acceptance Criteria:

- **Scenario**: list 输出全部故障目录 JSON 数组
- **Given**: demoncat.conf 声明了 rCPU_overload、rNET_loss、rPROC_exit 等故障
- **When**: 测试工程师执行 `dcat list`
- **Then**: 返回 `{"status":"ok","op":"list","data":[{"uid":"rCPU_overload","module":"cpu","supported_ops":["inject","clean","query"],"desc":"..."},...]}`，含全部已声明故障；不读 state.json、不调用脚本

- **Scenario**: list 受 `--config` 覆盖影响
- **Given**: 存在仅含 rCPU_overload 的测试配置 `/tmp/test.conf`
- **When**: 测试工程师执行 `dcat list --config /tmp/test.conf`
- **Then**: 仅列出该配置中的 rCPU_overload 条目

- **Scenario**: list 对 inject-only 故障也输出且 supported_ops 仅 inject
- **Given**: 配置含 rPROC_exit（`supported_ops=inject`）
- **When**: 测试工程师执行 `dcat list`
- **Then**: rPROC_exit 条目出现，其 `supported_ops` 为 `["inject"]`

---

## US-16 同步阻塞执行与长驻故障脚本自管理

- **Summary**: inject/clean/query 同步阻塞，长驻故障由脚本自行 spawn 子进程+pidfile 后返回

### Use Case:
- **As a** 关注注入时序与可观测性的测试工程师
- **I want to** inject/clean/query 同步阻塞（dcat fork/exec+waitpid 等脚本执行完才返回），需要长驻的故障由脚本自行 spawn 子进程并写 pidfile 后立即返回
- **so that** 我能明确知道注入何时生效，且 dcat 不被长驻故障阻塞挂起

### Acceptance Criteria:

- **Scenario**: 一次性 inject 同步执行完返回
- **Given**: rPROC_exit（inject-only，脚本 kill 后立即退出）
- **When**: 测试工程师执行 `dcat inject rPROC_exit --pid=12345`
- **Then**: dcat fork/exec 脚本，waitpid 等脚本执行完才返回结果 JSON，不提前返回

- **Scenario**: 长驻故障脚本 spawn 子进程写 pidfile 后立即返回
- **Given**: rCPU_overload 需长驻 burn 进程维持满载
- **When**: 测试工程师执行 `dcat inject rCPU_overload --cores=0,1`
- **Then**: 脚本 `taskset` 启动 burn 子进程后台运行、写 pidfile 后立即返回（不前台驻留），dcat 同步等到脚本返回即输出 `status:ok`；burn 子进程在后台持续满载

- **Scenario**: clean 读 pidfile 终止长驻子进程
- **Given**: rCPU_overload 已注入，pidfile 存在 burn 子进程 pid
- **When**: 测试工程师执行 `dcat clean rCPU_overload --cores=0,1`
- **Then**: 脚本读 pidfile 逐个 kill 子进程并删 pidfile 后返回，dcat 同步等待返回结果

---

## US-17 日志级别控制

- **Summary**: dcat 日志走 stderr，log_level 控制 debug/info/warn/error，生产默认 warn

### Use Case:
- **As a** 排查注入异常的测试工程师
- **I want to** 通过 `log_level` 控制 dcat stderr 日志级别（debug/info/warn/error），生产默认 warn，调试时设 debug 看详细分发过程
- **so that** 我能在调试时获得详细信息，生产时不被噪音淹没

### Acceptance Criteria:

- **Scenario**: 默认 warn 级别只输出告警以上
- **Given**: demoncat.conf `log_level=warn`（默认）
- **When**: 测试工程师执行一次正常 inject
- **Then**: stderr 仅输出 warn 及以上级别日志，debug/info 不输出

- **Scenario**: 设 debug 输出详细分发过程
- **Given**: 测试场景配置 `log_level=debug`
- **When**: 测试工程师执行 `dcat inject rNET_loss --iface=eth0 --loss_pct=5`
- **Then**: stderr 输出 debug 级别日志（含解析/查表/分发/precheck 等详细过程），便于排查；JSON 结果仍走 stdout 不受影响

- **Scenario**: 日志走 stderr 不污染 stdout JSON
- **Given**: 任意 log_level
- **When**: 测试工程师执行任一命令并捕获 stdout
- **Then**: stdout 仅含 JSON 结果（status/data/error），日志与脚本 stderr 均走 stderr，管道可纯净解析 JSON

---

## US-18 mock_executor 表驱动断言

- **Summary**: 通过 mock_executor 捕获下发命令串+env 做表驱动断言，无硬件可测全部故障

### Use Case:
- **As a** 无硬件环境下编写故障测试的测试工程师
- **I want to** 用 `executor_set_mock(fn)` 捕获 dcat 实际下发的命令串与环境变量（不真 fork），按表驱动断言每个故障 inject/clean/query 的命令串与 env
- **so that** 我能无硬件依赖地验证全部故障的下发命令正确性

### Acceptance Criteria:

- **Scenario**: mock 捕获命令串与环境变量不真 fork
- **Given**: 测试注册 mock_executor，`fn` 捕获 `(cmd, env)` 返回伪造 `result_t`
- **When**: 测试执行 `dcat inject rNET_loss --iface=eth0 --loss_pct=5`
- **Then**: mock 捕获到 cmd 含 `tc qdisc add dev eth0 root netem loss random 5%`、env 含 `DCAT_OP=inject`/`DCAT_UID=rNET_loss`/`DCAT_PARAM_IFACE=eth0`/`DCAT_PARAM_LOSS_PCT=5`，不真 fork、无硬件依赖

- **Scenario**: 表驱动断言覆盖每个故障三路径
- **Given**: `tests/ut/test_faults_*.c` 表驱动用例为每个故障声明 inject/clean/query 的期望命令串、env、退出码、JSON
- **When**: 运行 ctest
- **Then**: 全部 33 故障的 inject/clean/query（inject-only 仅 inject）下发命令串与 env 按表断言通过；任一不匹配则该用例失败

- **Scenario**: 真实脚本测试仅覆盖示例故障且不断言真硬件
- **Given**: 真实脚本测试用例覆盖 rCPU_overload / rNET_delay 两个示例故障
- **When**: 运行该用例
- **Then**: 用 mock_executor 断言下发命令串，不断言真 CPU 占用或真 tc 生效（真硬件验证走端到端冒烟）
