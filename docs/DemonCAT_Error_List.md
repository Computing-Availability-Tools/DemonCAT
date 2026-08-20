# DemonCAT 故障目录

当前共 58 条故障，覆盖 CPU / 内存 / 存储 / 网络 / 进程 / NPU / Docker / 文件系统 / 系统九个模块。

## CPU 模块（5 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rCPU_overload` | cores | load_pct | CPU overload (multi-core burn, pure user-space) |
| `rCPU_core_offline` | cores | — | CPU core offline via sysfs |
| `rCPU_core_hang` | cores | — | CPU core hang (isolating core, migration stop) |
| `rCPU_freq` | cores,freq_mhz | — | CPU frequency limit (cpufreq scaling) |
| `rCPU_quota` | cores,quota_pct | — | CPU core max utilization via cgroup |

## 存储模块（5 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rDISK_write_overload` | device | workers,size_mb | Disk write IO overload (dd writers) |
| `rDISK_part_full` | path | size | Partition fill (dd file creation) |
| `rDISK_inode_exhaust` | path | count | Inode exhaustion (small file creation) |
| `rDISK_io_delay` | device,delay_ms | — | Disk IO delay (device-mapper delay) |
| `rDISK_io_error` | device | — | Disk IO error (device-mapper error) |

## 网络模块（13 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rNET_delay` | iface,delay_ms | — | Network egress delay via tc netem |
| `rNET_loss` | iface,loss_pct | — | Network packet loss (tc netem) |
| `rNET_reorder` | iface,reorder_pct | — | Network packet reorder (tc netem) |
| `rNET_down` | iface | — | NIC down (ip link) |
| `rNET_degrade` | iface | speed_mbps | NIC speed degrade (tc tbf rate-limit) |
| `rNET_port_occupy` | port | protocol | Port occupation (socket holder) |
| `rNET_service_stop` | service | — | Service stop (systemctl) |
| `rNET_link_flap` | iface | cycle_sec,count | Link flap (ip link down/up loop) |
| `rNET_bw_limit` | iface,rate_kbps | — | Bandwidth limit (tc tbf) |
| `rNET_jitter` | iface,delay_ms,jitter_ms | — | Delay + jitter (tc netem) |
| `rNET_tcp_loss` | port | direction | TCP packet loss (iptables DROP) |
| `rNET_conn_exhaust` | target | count | Connection exhaustion (socket flood) |
| `rNET_corrupt` | iface,corrupt_pct | — | Packet corruption (tc netem) |

## 进程模块（5 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rPROC_exit` | pid | — | Process exit (kill -9, irreversible) |
| `rPROC_hang` | pid | — | Process hang (SIGSTOP) |
| `rPROC_zstate` | pid | — | Zombie process (kill target → zombie) |
| `rPROC_fork_bomb` | count | — | Fork bomb (controlled process explosion) |
| `rPROC_fd_exhaust` | count | — | File descriptor exhaustion |

## 内存模块（4 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rMEM_leak` | size_mb | — | Memory leak (progressive allocation) |
| `rMEM_oom` | rate_mb | — | OOM pressure (rapid allocation) |
| `rMEM_fragment` | blocks | — | Memory fragmentation (scatter allocation) |
| `rMEM_swap_overload` | size_mb | — | Swap overload (force swapping) |

## 文件系统模块（2 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rFS_file_lock` | path,mode | — | File lock (chmod+chattr+mount --bind: noread/nowrite/norw/nodelete) |
| `rFS_iowait_high` | path | workers | High iowait (parallel dd readers) |

## Docker 模块（2 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rDOCKER_kill` | container | — | Docker container kill |
| `rDOCKER_mem_overload` | container | size | Docker container memory overload (stress) |

## NPU 模块（20 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rNPU_link_down` | chip | — | RoCE link down (hccn_tool) |
| `rNPU_ip_change` | chip,address,netmask | — | RoCE IP change |
| `rNPU_gw_change` | chip,gateway | — | RoCE gateway change |
| `rNPU_netdetect_change` | chip,address | — | Netdetect IP change |
| `rNPU_arp` | chip,dev,ip,mac | — | ARP poison (inject adds fake ARP entry, clean deletes it) |
| `rNPU_route` | chip,address,netmask,gateway | — | RoCE route (inject adds route, clean deletes it) |
| `rNPU_iprule` | chip,dir,ip,table | — | ip rule (inject adds rule, clean deletes it) |
| `rNPU_iproute` | chip,ip,ip_mask,table,via,dev | — | ip route (inject adds route, clean deletes it) |
| `rNPU_bw_limit` | chip,bw_limit | — | RoCE bandwidth shaping limit |
| `rNPU_mtu_mismatch` | chip,size | — | RoCE MTU mismatch |
| `rNPU_dscp_tc_change` | chip,dscp,tc | — | DSCP-to-TC mapping change |
| `rNPU_roce_port_change` | chip,port | — | RoCE UDP port change |
| `rNPU_pcie_down` | npu_id | gen | PCIe link speed downgrade (Gen4→Gen1, setpci) |
| `rNPU_aic_load` | chip | load_pct | AICore stress (aclnnMatmul FP16) |
| `rNPU_aicpu_load` | chip | load_pct | AICpu stress (aclnnTopk FP64) |
| `rNPU_aiv_load` | chip | load_pct | AIVector stress (aclnnExp FP16) |
| `rNPU_hbm_load` | chip,size | — | HBM stress (aclrtMalloc+memset) |
| `rNPU_chip_reset` | npu_id | core | NPU chip reset (npu-smi set -t reset, 多芯片卡可能整卡重启) |
| `rNPU_driver_unbind` | chip | — | 驱动解绑 (PCIe unbind, 需重启恢复) |
| `rNPU_pcie_remove` | chip | — | PCIe 拔卡 (PCIe remove, 需冷启动恢复) |

## 系统模块（2 条）

| UID | 必填参数 | 可选参数 | 说明 |
| --- | --- | --- | --- |
| `rSYS_panic` | — | — | 系统崩溃 (kernel panic, 导致系统宕机) |
| `rSYS_poweroff` | mode | — | 系统关机 (直接断电关机) |
