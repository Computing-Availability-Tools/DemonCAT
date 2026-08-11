# DemonCAT 故障目录

当前共 33 条故障，覆盖 CPU / 存储 / 网络 / 进程 / NPU 五个模块。

## CPU 模块（2 条）

| UID | 必填参数 | 可选参数 | 说明 |
|---|---|---|---|
| `rCPU_overload` | cores | load_pct | CPU overload (multi-core burn, pure user-space) |
| `rCPU_core_offline` | cores | — | CPU core offline via sysfs |

## 存储模块（1 条）

| UID | 必填参数 | 可选参数 | 说明 |
|---|---|---|---|
| `rDISK_write_overload` | device | workers,size_mb | Disk write IO overload (dd writers) |

## 网络模块（11 条）

| UID | 必填参数 | 可选参数 | 说明 |
|---|---|---|---|
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

## 进程模块（3 条）

| UID | 必填参数 | 可选参数 | 说明 |
|---|---|---|---|
| `rPROC_exit` | pid | — | Process exit (kill -9, irreversible) |
| `rPROC_hang` | pid | — | Process hang (SIGSTOP) |
| `rPROC_zstate` | pid | — | Zombie process (kill target → zombie, clean kills parent to reap) |

## NPU 模块（16 条）

| UID | 必填参数 | 可选参数 | 说明 |
|---|---|---|---|
| `rNPU_link_down` | chip | — | RoCE link down (hccn_tool -cfg recovery) |
| `rNPU_ip_change` | chip,address,netmask | — | RoCE IP change |
| `rNPU_gw_change` | chip,gateway | — | RoCE gateway change |
| `rNPU_netdetect_change` | chip,address | — | Netdetect IP change |
| `rNPU_arp_poison` | chip,dev,ip,mac | — | ARP poisoning (add wrong mac) |
| `rNPU_arp_del` | chip,dev,ip | — | ARP entry deletion |
| `rNPU_route_add` | chip,address,netmask,gateway | — | Add RoCE route |
| `rNPU_route_del` | chip,address,netmask | — | Delete RoCE route |
| `rNPU_iprule_add` | chip,dir,ip,table | — | Add ip rule |
| `rNPU_iprule_del` | chip,dir,ip | — | Delete ip rule |
| `rNPU_iproute_add` | chip,ip,ip_mask,via,dev,table | — | Add ip route |
| `rNPU_iproute_del` | chip,ip,ip_mask,table | — | Delete ip route |
| `rNPU_bw_limit` | chip,bw_limit | — | RoCE bandwidth shaping limit |
| `rNPU_mtu_mismatch` | chip,size | — | RoCE MTU mismatch |
| `rNPU_dscp_tc_change` | chip,dscp,tc | — | DSCP-to-TC mapping change |
| `rNPU_roce_port_change` | chip,port | — | RoCE UDP port change |
