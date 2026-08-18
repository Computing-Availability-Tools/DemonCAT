#!/usr/bin/env python3
"""tests/e2e/gen_cases.py — dcat e2e 测试用例自动生成器

8 类分类（混沌工程 + 测试矩阵）：
  FUNC  : 功能基线 — 52 故障 inject→verify→clean→query 全链路 + query<uid> + 插件
  BOUND : 边界值 — 每参数类型系统性覆盖（整数越界/空值/格式错误/枚举非法）
  SEC   : 安全 — 命令注入(inject+clean+query) + 权限边界 + 主机安全(路径穿越/symlink)
  STATE : 状态一致性 — clean×2/--force/reinject 拒绝/query 幂等/并发 inject
  RES   : 韧性/自愈 — state 丢失/损坏/孤儿/幽灵/clean --all 幂等/state 表满/信号中断
  CLI   : CLI 接口 — 解析错误 + 帮助 + 退出码 + --config + 未知 uid
  CONC  : 并发竞争 — 同时 inject+clean / 双进程写 state / clean --all + inject
  INTER : 故障交互 — 多故障叠加 / clean 一个不影响其他 / clean --all 后逐 verify

用法: python3 tests/e2e/gen_cases.py [-o tests/e2e/cases.csv]
"""
import argparse
import configparser
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CONF = os.path.join(ROOT, "config", "demoncat.conf")

COLUMNS = [
    "id", "flow_id", "step", "module", "fault_uid", "phase",
    "command", "expected_exit_code", "expected_json",
    "verify_cmd", "verify_assert", "provision", "precondition", "expected_behavior",
]

DCAT = "./build/dcat"

# NPU chip/npu_id parameterized via env vars (default: chip=2, npu_id=2)
E2E_CHIP = os.environ.get("DCAT_E2E_CHIP", "2")
E2E_NPU_ID = os.environ.get("DCAT_E2E_NPU_ID", "2")

# ---- 每故障观测知识 ----
OBS = {
    "rCPU_overload": dict(module="cpu", inject_args="--cores=0 --load_pct=100",
        clean_args="--cores=0", provision="none", precondition="none",
        v_cmd="pgrep -x perl | wc -l", v_assert=">=1",
        c_cmd="pgrep -x perl | wc -l", c_assert="==0"),
    "rCPU_core_offline": dict(module="cpu", inject_args="--cores=1",
        clean_args="--cores=1", provision="none", precondition="root+sysfs_writable+allow_cpu_offline",
        v_cmd="cat /sys/devices/system/cpu/cpu1/online 2>/dev/null || echo NA", v_assert="eq:0",
        c_cmd="cat /sys/devices/system/cpu/cpu1/online 2>/dev/null || echo NA", c_assert="eq:1"),
    "rDISK_write_overload": dict(module="storage", inject_args="--device=/dev/shm --workers=2 --size_mb=10",
        clean_args="--device=/dev/shm", provision="none", precondition="none",
        v_cmd="ls /dev/shm/dcat.stress.* 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /dev/shm/dcat.stress.* 2>/dev/null | wc -l", c_assert="==0"),
    "rNET_delay": dict(module="network", inject_args="--iface={iface} --delay_ms=100",
        clean_args="--iface={iface}", provision="dummy_iface", precondition="root+tc+dummy_iface",
        v_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", v_assert=">=1",
        c_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", c_assert="==0"),
    "rNET_loss": dict(module="network", inject_args="--iface={iface} --loss_pct=5",
        clean_args="--iface={iface}", provision="dummy_iface", precondition="root+tc+dummy_iface",
        v_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", v_assert=">=1",
        c_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", c_assert="==0"),
    "rNET_reorder": dict(module="network", inject_args="--iface={iface} --reorder_pct=30",
        clean_args="--iface={iface}", provision="dummy_iface", precondition="root+tc+dummy_iface",
        v_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", v_assert=">=1",
        c_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", c_assert="==0"),
    "rNET_bw_limit": dict(module="network", inject_args="--iface={iface} --rate_kbps=1000",
        clean_args="--iface={iface}", provision="dummy_iface", precondition="root+tc+dummy_iface",
        v_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c tbf", v_assert=">=1",
        c_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c tbf", c_assert="==0"),
    "rNET_jitter": dict(module="network", inject_args="--iface={iface} --delay_ms=50 --jitter_ms=10",
        clean_args="--iface={iface}", provision="dummy_iface", precondition="root+tc+dummy_iface",
        v_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", v_assert=">=1",
        c_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", c_assert="==0"),
    "rNET_down": dict(module="network", inject_args="--iface={iface}",
        clean_args="--iface={iface}", provision="dummy_iface", precondition="root+ip+dummy_iface",
        v_cmd="ip -o link show dev {iface} 2>/dev/null | grep -o 'state [A-Z]*' | awk '{print $2}'", v_assert="eq:DOWN",
        c_cmd="ip -o link show dev {iface} 2>/dev/null | grep -o 'state [A-Z]*' | awk '{print $2}'", c_assert="ne:DOWN"),
    "rNET_link_flap": dict(module="network", inject_args="--iface={iface} --cycle_sec=1 --count=2",
        clean_args="--iface={iface}", provision="dummy_iface", precondition="root+ip+dummy_iface",
        v_cmd="ip -o link show dev {iface} 2>/dev/null | grep -o 'state [A-Z]*' | awk '{print $2}'", v_assert="nonempty",
        c_cmd="ip -o link show dev {iface} 2>/dev/null | grep -o 'state [A-Z]*' | awk '{print $2}'", c_assert="ne:DOWN"),
    "rNET_degrade": dict(module="network", inject_args="--iface={iface} --speed_mbps=10",
        clean_args="--iface={iface}", provision="dummy_iface", precondition="root+tc+dummy_iface",
        v_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c tbf", v_assert=">=1",
        c_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c tbf", c_assert="==0"),
    "rNET_port_occupy": dict(module="network", inject_args="--port={port}",
        clean_args="--port={port}", provision="free_port", precondition="none",
        v_cmd="ss -tlnp 2>/dev/null | grep -c ':{port}'", v_assert=">=1",
        c_cmd="ss -tlnp 2>/dev/null | grep -c ':{port}'", c_assert="==0"),
    "rNET_service_stop": dict(module="network", inject_args="--service={svc}",
        clean_args="--service={svc}", provision="noncritical_svc", precondition="root+systemctl+noncritical_svc",
        v_cmd="systemctl is-active {svc} 2>/dev/null", v_assert="eq:inactive",
        c_cmd="systemctl is-active {svc} 2>/dev/null", c_assert="ne:inactive"),
    "rNET_tcp_loss": dict(module="network", inject_args="--port={port}",
        clean_args="--port={port}", provision="free_port", precondition="root+iptables",
        v_cmd="iptables -L INPUT -n 2>/dev/null | grep -c 'dpt:{port}'", v_assert=">=1",
        c_cmd="iptables -L INPUT -n 2>/dev/null | grep -c 'dpt:{port}'", c_assert="==0"),
    "rPROC_hang": dict(module="process", inject_args="--pid={pid}",
        clean_args="--pid={pid}", provision="sleep_pid", precondition="none",
        v_cmd="awk '/^State:/{print $2}' /proc/{pid}/status 2>/dev/null", v_assert="eq:T",
        c_cmd="awk '/^State:/{print $2}' /proc/{pid}/status 2>/dev/null", c_assert="ne:T"),
    "rPROC_zstate": dict(module="process", inject_args="--pid={pid}",
        clean_args="--pid={pid}", provision="sleep_pid", precondition="none",
        v_cmd="awk '/^State:/{print $2}' /proc/{pid}/status 2>/dev/null", v_assert="eq:Z",
        c_cmd="ls /proc/{pid} 2>/dev/null | wc -l", c_assert="==0"),
    "rPROC_exit": dict(module="process", inject_args="--pid={pid}",
        clean_args="", provision="sleep_pid", precondition="none", inject_only=True,
        v_cmd="awk '/^State:/{print $2}' /proc/{pid}/status 2>/dev/null || echo NONE", v_assert="eq:Z",
        c_cmd="", c_assert=""),
    # NPU (16 faults)
    "rNPU_link_down": dict(module="npu", inject_args="--chip={e2e_chip}", clean_args="--chip={e2e_chip}",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -link -g 2>/dev/null", v_assert="contains:DOWN",
        c_cmd="hccn_tool -i {e2e_chip} -link -g 2>/dev/null", c_assert="notcontains:DOWN"),
    "rNPU_ip_change": dict(module="npu", inject_args="--chip={e2e_chip} --address=10.0.0.99 --netmask=255.255.255.0",
        clean_args="--chip={e2e_chip}", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -ip -g 2>/dev/null", v_assert="contains:10.0.0.99",
        c_cmd="hccn_tool -i {e2e_chip} -ip -g 2>/dev/null", c_assert="notcontains:10.0.0.99"),
    "rNPU_gw_change": dict(module="npu", inject_args="--chip={e2e_chip} --gateway=10.20.10.1",
        clean_args="--chip={e2e_chip}", provision="none", precondition="hccn_tool",
        setup_cmd="hccn_tool -i {e2e_chip} -link -s up 2>/dev/null; hccn_tool -i {e2e_chip} -gateway -g 2>/dev/null | grep -q '10.20.10.254' || hccn_tool -i {e2e_chip} -gateway -s gateway 10.20.10.254 2>/dev/null",
        v_cmd="hccn_tool -i {e2e_chip} -gateway -g 2>/dev/null", v_assert="contains:10.20.10.1",
        c_cmd="hccn_tool -i {e2e_chip} -gateway -g 2>/dev/null", c_assert="notcontains:10.20.10.1"),
    "rNPU_netdetect_change": dict(module="npu", inject_args="--chip={e2e_chip} --address=10.0.0.99",
        clean_args="--chip={e2e_chip}", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -netdetect -g 2>/dev/null", v_assert="contains:10.0.0.99",
        c_cmd="hccn_tool -i {e2e_chip} -netdetect -g 2>/dev/null", c_assert="notcontains:10.0.0.99"),
    "rNPU_arp": dict(module="npu", inject_args="--chip={e2e_chip} --dev=eth0 --ip=10.30.12.200 --mac=00:11:22:33:44:55",
        clean_args="--chip={e2e_chip} --dev=eth0 --ip=10.30.12.200", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -arp -g 2>/dev/null", v_assert="contains:00:11:22:33:44:55",
        c_cmd="hccn_tool -i {e2e_chip} -arp -g 2>/dev/null", c_assert="notcontains:00:11:22:33:44:55"),
    "rNPU_route": dict(module="npu", inject_args="--chip={e2e_chip} --address=10.30.40.0 --netmask=255.255.255.0 --gateway=10.20.10.1",
        clean_args="--chip={e2e_chip} --address=10.30.40.0 --netmask=255.255.255.0", provision="none", precondition="hccn_tool",
        setup_cmd="hccn_tool -i {e2e_chip} -link -s up 2>/dev/null",
        v_cmd="hccn_tool -i {e2e_chip} -route -g 2>/dev/null", v_assert="contains:10.30.40.0",
        c_cmd="hccn_tool -i {e2e_chip} -route -g 2>/dev/null", c_assert="notcontains:10.30.40.0"),
    "rNPU_iprule": dict(module="npu", inject_args="--chip={e2e_chip} --dir=from --ip=10.20.10.50 --table=150",
        clean_args="--chip={e2e_chip} --dir=from --ip=10.20.10.50", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -ip_rule -g 2>/dev/null", v_assert="contains:10.20.10.50",
        c_cmd="hccn_tool -i {e2e_chip} -ip_rule -g 2>/dev/null", c_assert="notcontains:10.20.10.50"),
    "rNPU_iproute": dict(module="npu", inject_args="--chip={e2e_chip} --ip=10.30.50.0 --ip_mask=24 --via=10.20.10.1 --dev=eth0 --table=100",
        clean_args="--chip={e2e_chip} --ip=10.30.50.0 --ip_mask=24 --table=100", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -ip_route -g table 100 2>/dev/null", v_assert="contains:10.30.50.0",
        c_cmd="hccn_tool -i {e2e_chip} -ip_route -g table 100 2>/dev/null", c_assert="notcontains:10.30.50.0"),
    "rNPU_bw_limit": dict(module="npu", inject_args="--chip={e2e_chip} --bw_limit=50000", clean_args="--chip={e2e_chip}",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -shaping -g 2>/dev/null | grep -oE 'bw_limit\\[[0-9]+'", v_assert="contains:50000",
        c_cmd="hccn_tool -i {e2e_chip} -shaping -g 2>/dev/null | grep -oE 'bw_limit\\[[0-9]+'", c_assert="notcontains:50000"),
    "rNPU_mtu_mismatch": dict(module="npu", inject_args="--chip={e2e_chip} --size=1280", clean_args="--chip={e2e_chip}",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -mtu -g 2>/dev/null | grep -oE 'mtu:[0-9]+'", v_assert="contains:1280",
        c_cmd="hccn_tool -i {e2e_chip} -mtu -g 2>/dev/null | grep -oE 'mtu:[0-9]+'", c_assert="notcontains:1280"),
    "rNPU_dscp_tc_change": dict(module="npu", inject_args="--chip={e2e_chip} --dscp=46 --tc=1", clean_args="--chip={e2e_chip} --dscp=46",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -dscp_to_tc -g dscp 46 2>/dev/null | awk '$1==46{print $2}'", v_assert="eq:1",
        c_cmd="hccn_tool -i {e2e_chip} -dscp_to_tc -g dscp 46 2>/dev/null | awk '$1==46{print $2}'", c_assert="eq:0"),
    "rNPU_roce_port_change": dict(module="npu", inject_args="--chip={e2e_chip} --port=4792", clean_args="--chip={e2e_chip}",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i {e2e_chip} -udp -g 2>/dev/null", v_assert="contains:4792",
        c_cmd="hccn_tool -i {e2e_chip} -udp -g 2>/dev/null", c_assert="contains:4791"),
    # ---- batch2 新增故障 (26 条, 排除 sys_panic/sys_poweroff) ----
    "rMEM_leak": dict(module="memory", inject_args="--size_mb=32", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rMEM_leak.pid 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rMEM_leak.pid 2>/dev/null | wc -l", c_assert="==0"),
    "rMEM_oom": dict(module="memory", inject_args="--rate_mb=256", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rMEM_oom.pid 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rMEM_oom.pid 2>/dev/null | wc -l", c_assert="==0"),
    "rMEM_fragment": dict(module="memory", inject_args="--blocks=20", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rMEM_fragment.pid 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rMEM_fragment.pid 2>/dev/null | wc -l", c_assert="==0"),
    "rMEM_swap_overload": dict(module="memory", inject_args="--size_mb=128", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rMEM_swap_overload.pid 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rMEM_swap_overload.pid 2>/dev/null | wc -l", c_assert="==0"),
    "rCPU_quota": dict(module="cpu", inject_args="--cores=0 --quota_pct=50", clean_args="",
        provision="none", precondition="root+cgroup",
        v_cmd="ls /tmp/dcat-rCPU_quota-*.sidecar 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rCPU_quota-*.sidecar 2>/dev/null | wc -l", c_assert="==0"),
    "rCPU_freq": dict(module="cpu", inject_args="--cores=0 --freq_mhz=1000", clean_args="",
        provision="none", precondition="root+cpufreq",
        v_cmd="cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null", v_assert="eq:1000000",
        c_cmd="cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null", c_assert="ne:1000000"),
    "rCPU_core_hang": dict(module="cpu", inject_args="--cores=0", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rCPU_core_hang-c*.pid 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rCPU_core_hang-c*.pid 2>/dev/null | wc -l", c_assert="==0"),
    "rDISK_part_full": dict(module="storage", inject_args="--path=/tmp --size=50M", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rDISK_part_full-*.sidecar 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rDISK_part_full-*.sidecar 2>/dev/null | wc -l", c_assert="==0"),
    "rDISK_inode_exhaust": dict(module="storage", inject_args="--path=/tmp --count=500", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rDISK_inode_exhaust-*.sidecar 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rDISK_inode_exhaust-*.sidecar 2>/dev/null | wc -l", c_assert="==0"),
    "rDISK_io_delay": dict(module="storage", inject_args="--device={loop_dev} --delay_ms=10", clean_args="",
        provision="loop_device", precondition="root+dm_delay",
        v_cmd="ls /tmp/dcat-rDISK_io_delay-*.sidecar 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rDISK_io_delay-*.sidecar 2>/dev/null | wc -l", c_assert="==0"),
    "rNET_corrupt": dict(module="network", inject_args="--iface={iface} --corrupt_pct=10", clean_args="",
        provision="dummy_iface", precondition="root+tc+dummy_iface",
        v_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", v_assert=">=1",
        c_cmd="tc qdisc show dev {iface} 2>/dev/null | grep -c netem", c_assert="==0"),
    "rPROC_fork_bomb": dict(module="process", inject_args="--count=20", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rPROC_fork_bomb.pid 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rPROC_fork_bomb.pid 2>/dev/null | wc -l", c_assert="==0"),
    "rPROC_fd_exhaust": dict(module="process", inject_args="--count=256", clean_args="",
        provision="none", precondition="none",
        v_cmd="ls /tmp/dcat-rPROC_fd_exhaust.pid 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rPROC_fd_exhaust.pid 2>/dev/null | wc -l", c_assert="==0"),
    "rFS_file_lock": dict(module="filesystem", inject_args="--path=/tmp/dcat_fslock_test --mode=nowrite", clean_args="",
        provision="none", precondition="none",
        setup_cmd="echo test > /tmp/dcat_fslock_test",
        v_cmd="stat -c %a /tmp/dcat_fslock_test 2>/dev/null", v_assert="eq:444",
        c_cmd="stat -c %a /tmp/dcat_fslock_test 2>/dev/null", c_assert="ne:444"),
    "rNPU_pcie_down": dict(module="npu", inject_args="--npu_id={e2e_npu_id} --gen=1", clean_args="--npu_id={e2e_npu_id}",
        provision="none", precondition="hccn_tool",
        v_cmd="ls /tmp/dcat-rNPU_pcie_down-{e2e_npu_id}.bak 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat-rNPU_pcie_down-{e2e_npu_id}.bak 2>/dev/null | wc -l", c_assert="==0"),
    "rNPU_aic_load": dict(module="npu", inject_args="--chip={e2e_chip} --load_pct=100", clean_args="--chip={e2e_chip}",
        provision="none", precondition="hccn_tool",
        v_cmd=f"{DCAT} query rNPU_aic_load --chip={{e2e_chip}} 2>/dev/null | grep -c 'CONFIRMED'", v_assert=">=1",
        c_cmd=f"{DCAT} query rNPU_aic_load --chip={{e2e_chip}} 2>/dev/null | grep -c 'CONFIRMED'", c_assert="==0"),
    "rNPU_aiv_load": dict(module="npu", inject_args="--chip={e2e_chip} --load_pct=100", clean_args="--chip={e2e_chip}",
        provision="none", precondition="hccn_tool",
        v_cmd=f"{DCAT} query rNPU_aiv_load --chip={{e2e_chip}} 2>/dev/null | grep -c 'CONFIRMED'", v_assert=">=1",
        c_cmd=f"{DCAT} query rNPU_aiv_load --chip={{e2e_chip}} 2>/dev/null | grep -c 'CONFIRMED'", c_assert="==0"),
    "rNPU_hbm_load": dict(module="npu", inject_args="--chip={e2e_chip} --size=2G", clean_args="--chip={e2e_chip}",
        provision="none", precondition="hccn_tool",
        v_cmd=f"{DCAT} query rNPU_hbm_load --chip={{e2e_chip}} 2>/dev/null | grep -c 'CONFIRMED'", v_assert=">=1",
        c_cmd=f"{DCAT} query rNPU_hbm_load --chip={{e2e_chip}} 2>/dev/null | grep -c 'CONFIRMED'", c_assert="==0"),
    "rNPU_chip_reset": dict(module="npu", inject_args="--npu_id={e2e_npu_id}", clean_args="--npu_id={e2e_npu_id}",
        provision="none", precondition="npu-smi",
        v_cmd="ls /tmp/dcat-rNPU_chip_reset-{e2e_npu_id}-0.bak 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="npu-smi info 2>/dev/null | grep -c 'OK'", c_assert=">=1"),
    "rNPU_driver_unbind": dict(module="npu", inject_args="--chip=5",
        provision="none", precondition="root+npu-smi",
        v_cmd="ls /tmp/dcat-rNPU_driver_unbind-5.bak 2>/dev/null | wc -l", v_assert=">=1"),
    "rNPU_pcie_remove": dict(module="npu", inject_args="--chip=5",
        provision="none", precondition="root+npu-smi",
        v_cmd="ls /tmp/dcat-rNPU_pcie_remove-5.bak 2>/dev/null | wc -l", v_assert=">=1"),
}


def gen():
    rows = []
    seq = [0]

    def nid(prefix):
        seq[0] += 1
        return f"{prefix}-{seq[0]:03d}"

    def add(flow, step, module, uid, phase, precond, cmd, exp_code, exp_json,
            vcmd="", vassert="", prov="", behavior=""):
        rows.append({
            "id": nid("E2E"), "flow_id": flow, "step": step, "module": module,
            "fault_uid": uid, "phase": phase,
            "command": cmd, "expected_exit_code": exp_code, "expected_json": exp_json,
            "verify_cmd": vcmd, "verify_assert": vassert, "provision": prov,
            "precondition": precond, "expected_behavior": behavior,
        })

    # ================================================================
    # FUNC: 功能基线 (inject→verify→clean→query + query<uid> + plugin)
    # Dangerous faults are skipped (require cold boot to recover)
    SKIP_FLOWS = {"rNPU_driver_unbind", "rNPU_pcie_remove", "rNPU_link_down", "rSYS_panic", "rSYS_poweroff"}
    # ================================================================
    for uid in sorted(OBS):
        o = OBS[uid]
        real_uid = o.get("real_uid", uid)
        flow = f"FUNC-{uid}"
        if uid in SKIP_FLOWS:
            add(flow, 0, o["module"], real_uid, "skip", "none",
                "echo 'SKIP: dangerous fault (requires cold boot to recover)'", 0, "",
                "", "", "", "skipped: dangerous fault"); continue
        s = 0
        if o.get("provision", "none") != "none" or o.get("setup_cmd"):
            add(flow, s, o["module"], real_uid, "setup", o["precondition"], o.get("setup_cmd", ""), 0, "",
                "", "", o.get("provision", "none"), "provision " + o.get("provision", "none")); s += 1
        add(flow, s, o["module"], real_uid, "inject", o["precondition"],
            f"{DCAT} inject {real_uid} {o['inject_args']}", 0, '"status":"ok"',
            o["v_cmd"], o["v_assert"], "", "fault active after inject"); s += 1
        if not o.get("inject_only"):
            add(flow, s, o["module"], real_uid, "clean", o["precondition"],
                f"{DCAT} clean {real_uid} {o['clean_args']}", 0, "", o["c_cmd"], o["c_assert"],
                "", "fault cleaned, system restored"); s += 1
            add(flow, s, o["module"], real_uid, "query", o["precondition"],
                f"{DCAT} query", 0, "", "", f"state_not_contains:{real_uid}", "",
                "no ghost record after clean"); s += 1
        else:
            add(flow, s, o["module"], real_uid, "clean_rejected", o["precondition"],
                f"{DCAT} clean {real_uid} {o['inject_args']}", 3, "", "", "exitcode:3",
                "", "inject-only: clean rejected"); s += 1

    # FUNC-Q: query<uid> confirmed (representative non-root faults)
    s_q = 1
    def q_case(uid, inject_args, query_args, provision):
        nonlocal s_q
        flow = f"FUNC-Q{s_q}"
        add(flow, 0, OBS[uid]["module"], uid, "inject", "none",
            f"{DCAT} inject {uid} {inject_args}", 0, '"status":"ok"', "", "", provision, "inject")
        add(flow, 1, OBS[uid]["module"], uid, "query_active", "none",
            f"{DCAT} query {uid} {query_args}", 0, "", "", 'out_contains:"confirmed":true', "", "query<uid> confirmed:true")
        add(flow, 2, OBS[uid]["module"], uid, "clean", "none",
            f"{DCAT} clean {uid} {inject_args}", 0, "", "", "", "", "cleanup")
        add(flow, 3, OBS[uid]["module"], uid, "query_gone", "none",
            f"{DCAT} query {uid} {query_args}", 0, "", "", 'out_contains:"confirmed":false', "", "query<uid> confirmed:false")
        s_q += 1
    q_case("rCPU_overload", "--cores=0", "--cores=0", "")
    q_case("rNET_port_occupy", "--port={port}", "--port={port}", "free_port")
    q_case("rPROC_hang", "--pid={pid}", "--pid={pid}", "sleep_pid")
    q_case("rPROC_zstate", "--pid={pid}", "--pid={pid}", "sleep_pid")
    # FUNC-Q6: NPU query<uid> confirmed (mtu_mismatch)
    q_case("rNPU_mtu_mismatch", "--chip={e2e_chip} --size=1280", "--chip={e2e_chip}", "")
    # FUNC-Q7: NPU query<uid> confirmed (bw_limit)
    q_case("rNPU_bw_limit", "--chip={e2e_chip} --bw_limit=50000", "--chip={e2e_chip}", "")
    # FUNC-Q5: query<uid> no-params
    flow = f"FUNC-Q{s_q}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none",
        f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "query_noparam", "none",
        f"{DCAT} query rCPU_overload", 0, "", "", 'out_contains:"confirmed":true', "", "query<uid> no-params confirmed:true")
    add(flow, 2, "cpu", "rCPU_overload", "clean", "none",
        f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "cleanup")
    add(flow, 3, "cpu", "rCPU_overload", "query_gone", "none",
        f"{DCAT} query rCPU_overload", 0, "", "", 'out_contains:"confirmed":false', "", "query<uid> no-params confirmed:false")
    s_q += 1

    # FUNC-PLG: plugin lifecycle
    flow = "FUNC-PLG"
    add(flow, 0, "plugin", "rSAMPLE_test", "inject", "none",
        f"{DCAT} inject rSAMPLE_test", 0, '"status":"ok"', "", "", "", "plugin inject")
    add(flow, 1, "plugin", "rSAMPLE_test", "query_active", "none",
        f"{DCAT} query rSAMPLE_test", 0, "", "", 'out_contains:sample confirmed', "", "plugin query confirmed")
    add(flow, 2, "plugin", "rSAMPLE_test", "clean", "none",
        f"{DCAT} clean rSAMPLE_test", 0, "", "", "", "", "plugin clean")
    add(flow, 3, "plugin", "rSAMPLE_test", "query_empty", "none",
        f"{DCAT} query", 0, "", "", "state_not_contains:rSAMPLE_test", "", "plugin state empty")

    # ================================================================
    # BOUND: 边界值 (每参数类型系统性覆盖)
    # ================================================================
    s_b = 1
    def b_reject(uid, args, exp_code, exp_json, behavior):
        nonlocal s_b
        flow = f"BOUND-{s_b}"
        add(flow, 0, OBS[uid]["module"], uid, "inject", "none",
            f"{DCAT} inject {uid} {args}", exp_code, exp_json, "", "exitcode:" + str(exp_code), "", behavior)
        s_b += 1
    def b_gap(uid, args, clean_args, behavior):
        nonlocal s_b
        flow = f"BOUND-{s_b}"
        add(flow, 0, OBS[uid]["module"], uid, "inject", "none",
            f"{DCAT} inject {uid} {args}", 0, '"status":"ok"', "", "", "", behavior + " (GAP)")
        add(flow, 1, OBS[uid]["module"], uid, "clean", "none",
            f"{DCAT} clean {uid} {clean_args}", 0, "", "", "", "", "cleanup")
        s_b += 1

    # cores (integer list/range)
    b_reject("rCPU_overload", "--cores=0/1", 1, 'invalid cores spec', "cores: invalid separator /")
    b_reject("rCPU_overload", "--cores=abc", 1, 'invalid cores spec', "cores: non-numeric")
    b_reject("rCPU_overload", "--cores=", 3, 'missing required parameter', "cores: empty = missing")
    b_reject("rCPU_overload", "", 3, 'missing required parameter', "cores: missing")
    b_gap("rCPU_overload", "--cores=0-999", "--cores=0-999", "cores: 0-999 no range cap")
    b_gap("rCPU_overload", "--cores=3-1", "--cores=3-1", "cores: 3-1 lo>hi")
    b_gap("rCPU_overload", "--cores=0,1,", "--cores=0,1,", "cores: trailing comma")
    # load_pct (integer range 1-100)
    b_reject("rCPU_overload", "--cores=0 --load_pct=0", 1, 'load_pct must be', "load_pct: 0 below range")
    b_reject("rCPU_overload", "--cores=0 --load_pct=500", 1, 'load_pct must be', "load_pct: 500 above range")
    b_reject("rCPU_overload", "--cores=0 --load_pct=-1", 1, 'load_pct must be', "load_pct: -1 negative")
    b_reject("rCPU_overload", "--cores=0 --load_pct=abc", 1, 'load_pct must be', "load_pct: non-numeric")
    # port (integer 1-65535)
    b_reject("rNET_port_occupy", "--port=abc", 1, 'port must be numeric', "port: non-numeric rejected")
    b_reject("rNET_port_occupy", "--port=65536", 1, 'port must be 1-65535', "port: 65536 above range")
    b_reject("rNET_port_occupy", "--port=0", 1, 'port must be 1-65535', "port: 0 below range")
    # loss_pct (integer 0-100)
    b_reject("rNET_loss", "--iface=dcat-e2e0 --loss_pct=-1", 1, 'loss_pct must be', "loss_pct: -1 negative")
    b_reject("rNET_loss", "--iface=dcat-e2e0 --loss_pct=101", 1, 'loss_pct must be', "loss_pct: 101 above 100")
    b_reject("rNET_loss", "--iface=dcat-e2e0 --loss_pct=abc", 1, 'loss_pct must be', "loss_pct: non-numeric")
    # delay_ms (positive integer)
    b_reject("rNET_delay", "--iface=dcat-e2e0 --delay_ms=-1", 1, '', "delay_ms: -1 negative")
    b_reject("rNET_delay", "--iface=dcat-e2e0 --delay_ms=abc", 1, '', "delay_ms: non-numeric")
    # chip (single digit 0-9)
    b_reject("rNPU_bw_limit", "--chip=12 --bw_limit=10000", 1, '', "chip: 12 不存在的 NPU 设备(hccn_tool 拒绝)")
    b_reject("rNPU_bw_limit", "--chip=a --bw_limit=10000", 1, '', "chip: 非数字被拒绝")
    b_reject("rNPU_bw_limit", "--bw_limit=10000", 3, 'missing required parameter', "chip: missing")
    # pid (positive integer)
    b_reject("rPROC_hang", "--pid=0", 1, '', "pid: 0 invalid")
    b_reject("rPROC_hang", "--pid=-1", 1, '', "pid: -1 negative")
    b_reject("rPROC_hang", "--pid=abc", 1, '', "pid: non-numeric")
    b_reject("rPROC_hang", "--pid=99999999", 1, '', "pid: nonexistent")
    # workers (positive integer)
    b_reject("rDISK_write_overload", "--device=/tmp --workers=0", 1, '', "workers: 0 invalid")
    b_reject("rDISK_write_overload", "--device=/tmp --workers=-1", 1, '', "workers: -1 negative")
    b_reject("rDISK_write_overload", "--device=/tmp --workers=abc", 1, '', "workers: non-numeric")
    # size_mb (positive integer)
    b_reject("rDISK_write_overload", "--device=/tmp --size_mb=0", 1, '', "size_mb: 0 invalid")
    b_reject("rDISK_write_overload", "--device=/tmp --size_mb=-1", 1, '', "size_mb: -1 negative")
    # device (path)
    b_reject("rDISK_write_overload", "--device=/nonexistent_dir_xyz", 1, '', "device: nonexistent path")
    # iface (string)
    b_reject("rNET_delay", "--iface= --delay_ms=100", 3, 'missing required parameter', "iface: empty")
    # service (string)
    b_reject("rNET_service_stop", "--service=", 3, 'missing required parameter', "service: empty")
    # encoding (enum)
    b_reject("rNPU_ip_change", "--chip={e2e_chip} --address=invalid --netmask=255.255.255.0", 1, '', "address: invalid format")
    # bitmap (comma-separated digits)
    # bw_limit (positive integer)
    b_reject("rNPU_bw_limit", "--chip={e2e_chip} --bw_limit=0", 1, '', "bw_limit: 0 invalid")
    b_reject("rNPU_bw_limit", "--chip={e2e_chip} --bw_limit=-1", 1, '', "bw_limit: negative")
    b_reject("rNPU_bw_limit", "--chip={e2e_chip} --bw_limit=abc", 1, '', "bw_limit: non-numeric")
    # size (positive integer, 68-9702)
    b_reject("rNPU_mtu_mismatch", "--chip={e2e_chip} --size=0", 1, '', "size: 0 invalid")
    b_reject("rNPU_mtu_mismatch", "--chip={e2e_chip} --size=-1", 1, '', "size: negative")
    b_reject("rNPU_mtu_mismatch", "--chip={e2e_chip} --size=abc", 1, '', "size: non-numeric")
    # port (positive integer)
    b_reject("rNPU_roce_port_change", "--chip={e2e_chip} --port=0", 1, '', "port: 0 invalid")
    b_reject("rNPU_roce_port_change", "--chip={e2e_chip} --port=-1", 1, '', "port: negative")
    b_reject("rNPU_roce_port_change", "--chip={e2e_chip} --port=abc", 1, '', "port: non-numeric")
    # dscp (0-63)
    b_reject("rNPU_dscp_tc_change", "--chip={e2e_chip} --dscp=64 --tc=1", 1, '', "dscp: 64 above range")
    b_reject("rNPU_dscp_tc_change", "--chip={e2e_chip} --dscp=-1 --tc=1", 1, '', "dscp: negative")

    # ---- batch2 新增参数类型边界值 ----
    # quota_pct (1-99 range)
    b_reject("rCPU_quota", "--cores=0 --quota_pct=0", 1, 'quota_pct must be', "quota_pct: 0 below range")
    b_reject("rCPU_quota", "--cores=0 --quota_pct=100", 1, 'quota_pct must be', "quota_pct: 100 above range")
    b_reject("rCPU_quota", "--cores=0 --quota_pct=-1", 1, 'quota_pct must be', "quota_pct: negative")
    b_reject("rCPU_quota", "--cores=0 --quota_pct=abc", 1, 'quota_pct must be', "quota_pct: non-numeric")
    b_reject("rCPU_quota", "--quota_pct=50", 3, 'missing required parameter', "cores: missing")
    b_reject("rCPU_quota", "--cores=0", 3, 'missing required parameter', "quota_pct: missing")
    b_reject("rCPU_quota", "", 3, 'missing required parameter', "cores+quota_pct: missing")
    # freq_mhz (positive integer)
    b_reject("rCPU_freq", "--cores=0 --freq_mhz=0", 1, '', "freq_mhz: 0 invalid")
    b_reject("rCPU_freq", "--cores=0 --freq_mhz=-1", 1, '', "freq_mhz: negative")
    b_reject("rCPU_freq", "--cores=0 --freq_mhz=abc", 1, '', "freq_mhz: non-numeric")
    # corrupt_pct (0-100 range)
    b_reject("rNET_corrupt", "--iface=dcat-e2e0 --corrupt_pct=-1", 1, '', "corrupt_pct: negative")
    b_reject("rNET_corrupt", "--iface=dcat-e2e0 --corrupt_pct=101", 1, '', "corrupt_pct: 101 above 100")
    b_reject("rNET_corrupt", "--iface=dcat-e2e0 --corrupt_pct=abc", 1, '', "corrupt_pct: non-numeric")
    # blocks (positive integer)
    b_reject("rMEM_fragment", "--blocks=0", 1, '', "blocks: 0 invalid")
    b_reject("rMEM_fragment", "--blocks=-1", 1, '', "blocks: negative")
    b_reject("rMEM_fragment", "--blocks=abc", 1, '', "blocks: non-numeric")
    # count (positive integer, rPROC_fork_bomb)
    b_reject("rPROC_fork_bomb", "--count=0", 1, '', "count: 0 invalid")
    b_reject("rPROC_fork_bomb", "--count=-1", 1, '', "count: negative")
    b_reject("rPROC_fork_bomb", "--count=abc", 1, '', "count: non-numeric")
    # rate_mb (positive integer)
    b_reject("rMEM_oom", "--rate_mb=0", 1, '', "rate_mb: 0 invalid")
    b_reject("rMEM_oom", "--rate_mb=-1", 1, '', "rate_mb: negative")
    b_reject("rMEM_oom", "--rate_mb=abc", 1, '', "rate_mb: non-numeric")
    # size_mb (positive integer, rMEM_leak)
    b_reject("rMEM_leak", "--size_mb=0", 1, '', "size_mb: 0 invalid")
    b_reject("rMEM_leak", "--size_mb=-1", 1, '', "size_mb: negative")
    b_reject("rMEM_leak", "--size_mb=abc", 1, '', "size_mb: non-numeric")
    # mode (enum noread/nowrite/norw/nodelete)
    b_reject("rFS_file_lock", "--path=/tmp --mode=invalid", 1, 'mode must be one of', "mode: invalid enum")
    b_reject("rFS_file_lock", "--path=/tmp", 3, 'missing required parameter', "mode: missing")
    # path (directory, rDISK_part_full)
    b_reject("rDISK_part_full", "--path=/nonexistent_dir_xyz", 1, '', "path: not a directory")
    b_reject("rDISK_part_full", "--path=", 3, 'missing required parameter', "path: empty")

    # ---- NPU 新故障 BOUND ----
    # chip (NPU aic/aiv/hbm)
    b_reject("rNPU_aic_load", "--chip=abc", 1, '', "chip: non-numeric")
    b_reject("rNPU_aic_load", "", 3, 'missing required parameter', "chip: missing")
    b_reject("rNPU_aiv_load", "--chip=abc", 1, '', "chip: non-numeric")
    b_reject("rNPU_aiv_load", "", 3, 'missing required parameter', "chip: missing")
    b_reject("rNPU_hbm_load", "--chip=abc --size=2G", 1, '', "chip: non-numeric")
    b_reject("rNPU_hbm_load", "", 3, 'missing required parameter', "chip: missing")
    # npu_id + gen (NPU pcie_down)
    b_reject("rNPU_pcie_down", "--npu_id=abc --gen=1", 1, '', "npu_id: non-numeric")
    b_reject("rNPU_pcie_down", "--npu_id={e2e_npu_id} --gen=abc", 1, '', "gen: non-numeric")
    b_reject("rNPU_pcie_down", "--npu_id={e2e_npu_id} --gen=5", 1, '', "gen: out of range (1-3)")
    b_reject("rNPU_pcie_down", "", 3, 'missing required parameter', "npu_id: missing")
    # npu_id (NPU chip_reset)
    b_reject("rNPU_chip_reset", "--npu_id=abc", 1, '', "npu_id: non-numeric")
    b_reject("rNPU_chip_reset", "--npu_id=99", 1, '', "npu_id: nonexistent NPU")
    b_reject("rNPU_chip_reset", "", 3, 'missing required parameter', "npu_id: missing")

    # ================================================================
    # SEC: 安全 (命令注入 inject+clean+query + 权限边界 + 主机安全)
    # ================================================================
    PAYLOADS = [";touch /tmp/dcat_pwned", "$(touch /tmp/dcat_pwned)", "|touch /tmp/dcat_pwned"]
    # SEC-I: inject 路径命令注入
    INJ_TARGETS = [
        ("rNET_down", "--iface=dcat-e2e0{x}"),
        ("rNET_port_occupy", "--port=19999{x}"),
        ("rCPU_overload", "--cores=0{x}"),
        ("rDISK_write_overload", "--device=/tmp{x}"),
        ("rNET_service_stop", "--service=cron{x}"),
        ("rNPU_bw_limit", "--chip={e2e_chip}{x} --bw_limit=10000"),
        ("rNPU_ip_change", "--chip={e2e_chip} --address=1.1.1.1{x} --netmask=255.255.255.0"),
        ("rFS_file_lock", "--path=/tmp{x} --mode=nowrite"),
        ("rDISK_part_full", "--path=/tmp{x} --size=10M"),
        ("rMEM_leak", "--size_mb=32{x}"),
    ]
    s_i = 1
    for uid, argtmpl in INJ_TARGETS:
        for p in PAYLOADS:
            flow = f"SEC-I{s_i}"
            args = argtmpl.replace("{x}", p)
            add(flow, 0, OBS[uid]["module"], uid, "inject", "none",
                f"{DCAT} inject {uid} {args}", "*", "",
                "test -e /tmp/dcat_pwned && echo FOUND || echo CLEAN", "eq:CLEAN", "",
                f"inject payload not executed: {p}")
            s_i += 1
    # SEC-C: clean 路径命令注入 (通过 state 记录参数传递)
    s_c = 1
    CLEAN_TARGETS = [
        ("rCPU_overload", "--cores=0{x}", "--cores=0{x}"),
        ("rNET_port_occupy", "--port=19999{x}", "--port=19999{x}"),
        ("rDISK_write_overload", "--device=/tmp{x}", "--device=/tmp{x}"),
    ]
    for uid, inj_args_tmpl, clean_args_tmpl in CLEAN_TARGETS:
        for p in PAYLOADS:
            flow = f"SEC-C{s_c}"
            inj_args = inj_args_tmpl.replace("{x}", p)
            clean_args = clean_args_tmpl.replace("{x}", p)
            add(flow, 0, OBS[uid]["module"], uid, "inject", "none",
                f"{DCAT} inject {uid} {inj_args}", "*", "", "", "", "", "inject with payload")
            add(flow, 1, OBS[uid]["module"], uid, "clean", "none",
                f"{DCAT} clean {uid} {clean_args}", "*", "",
                "test -e /tmp/dcat_pwned && echo FOUND || echo CLEAN", "eq:CLEAN", "",
                f"clean payload not executed: {p}")
            s_c += 1
    # SEC-P: 权限边界 (非 root 拒绝)
    s_p = 1
    def p_case(uid, args, vcmd, vassert, behavior):
        nonlocal s_p
        flow = f"SEC-P{s_p}"
        add(flow, 0, OBS[uid]["module"], uid, "inject", "non_root",
            f"{DCAT} inject {uid} {args}", "nonzero", "", vcmd, vassert, "", behavior)
        add(flow, 1, OBS[uid]["module"], uid, "query_noghost", "none",
            f"{DCAT} query", 0, "", "", f"state_not_contains:{uid}", "", "no ghost on perm fail")
        s_p += 1
    p_case("rNET_delay", "--iface=dcat-e2e0 --delay_ms=100",
           "tc qdisc show dev dcat-e2e0 2>/dev/null | grep -c netem", "==0", "non-root: no qdisc")
    p_case("rNET_tcp_loss", "--port=19998",
           "iptables -L INPUT -n 2>/dev/null | grep -c 'dpt:19998'", "==0", "non-root: no iptables")
    # SEC-H: 主机安全
    s_h = 1
    add(f"SEC-H{s_h}", 0, "network", "rNET_down", "inject", "none",
        f"{DCAT} inject rNET_down --iface=eth0-mgmt-test", "nonzero", "",
        "", "", "", "no mgmt-iface guard"); s_h += 1
    add(f"SEC-H{s_h}", 0, "network", "rNET_service_stop", "inject", "none",
        f"{DCAT} inject rNET_service_stop --service=sshd-test", "nonzero", "",
        "", "", "", "no sshd guard"); s_h += 1
    h3 = f"SEC-H{s_h}"
    add(h3, 0, "storage", "rDISK_write_overload", "inject", "none",
        f"{DCAT} inject rDISK_write_overload --device=/tmp --workers=2 --size_mb=200", 0, '"status":"ok"',
        "ls /etc/dcat.stress.* /etc/dcat.write.* 2>/dev/null | wc -l", "==0", "",
        "write containment: /tmp only")
    add(h3, 1, "storage", "rDISK_write_overload", "clean", "none",
        f"{DCAT} clean rDISK_write_overload --device=/tmp", 0, "", "", "", "", "cleanup")
    s_h += 1
    add(f"SEC-H{s_h}", 0, "cpu", "rCPU_overload", "inject", "none",
        f"{DCAT} inject rCPU_overload --cores=0 --bogus=1", 3, 'unknown parameter', "", "exitcode:3",
        "", "undeclared param rejected"); s_h += 1
    # SEC-S: symlink 攻击 (device 指向 /etc 的 symlink)
    add(f"SEC-S1", 0, "storage", "rDISK_write_overload", "inject", "none",
        "ln -sf /etc /tmp/dcat_symtest 2>/dev/null; " + f"{DCAT} inject rDISK_write_overload --device=/tmp/dcat_symtest --workers=1 --size_mb=1", "*", "",
        "ls /etc/dcat.stress.* 2>/dev/null | wc -l", "==0", "", "symlink: no write to /etc")
    add("SEC-S1", 1, "storage", "rDISK_write_overload", "clean", "none",
        f"{DCAT} clean rDISK_write_overload --device=/tmp/dcat_symtest; rm -f /tmp/dcat_symtest", 0, "", "", "", "", "cleanup symlink")

    # ================================================================
    # STATE: 状态一致性与幂等性
    # ================================================================
    s_s = 1
    # S1: clean --params ×2 → 第二次 code1
    flow = f"STATE-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "clean1", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "pgrep -x perl | wc -l", "==0", "", "first clean ok")
    add(flow, 2, "cpu", "rCPU_overload", "clean2", "none", f"{DCAT} clean rCPU_overload --cores=0", 1, "", "", "", "", "second clean: no active (dispatch returns 1)")
    s_s += 1
    # S2: 无参 clean ×2
    flow = f"STATE-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "clean1", "none", f"{DCAT} clean rCPU_overload", 0, "", "pgrep -x perl | wc -l", "==0", "", "no-arg clean ok")
    add(flow, 2, "cpu", "rCPU_overload", "clean2", "none", f"{DCAT} clean rCPU_overload", 0, "", "", "", "", "no-arg clean idempotent")
    add(flow, 3, "cpu", "rCPU_overload", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_not_contains:rCPU_overload", "", "no ghost")
    s_s += 1
    # S3: --force 替换
    flow = f"STATE-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject1", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject1")
    add(flow, 1, "cpu", "rCPU_overload", "force_replace", "none", f"{DCAT} inject rCPU_overload --cores=0 --force", 0, '"status":"ok"', "pgrep -x perl | wc -l", "<=2", "", "--force replace")
    add(flow, 2, "cpu", "rCPU_overload", "query_one", "none", f"{DCAT} query", 0, "", "", "state_contains:rCPU_overload", "", "one record")
    add(flow, 3, "cpu", "rCPU_overload", "clean", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "cleanup")
    s_s += 1
    # S4: reinject 默认拒绝 code5
    flow = f"STATE-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject1", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject1")
    add(flow, 1, "cpu", "rCPU_overload", "reject", "none", f"{DCAT} inject rCPU_overload --cores=0", 5, "", "pgrep -x perl | wc -l", ">=1", "", "reinject rejected code5")
    add(flow, 2, "cpu", "rCPU_overload", "clean", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "cleanup")
    s_s += 1
    # S5: query ×2 一致
    flow = f"STATE-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "query1", "none", f"{DCAT} query", 0, "", "", "state_contains:rCPU_overload", "", "query sees it")
    add(flow, 2, "cpu", "rCPU_overload", "query2", "none", f"{DCAT} query", 0, "", "", "state_contains:rCPU_overload", "", "query idempotent")
    add(flow, 3, "cpu", "rCPU_overload", "clean", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "cleanup")
    s_s += 1
    # S6: 不同资源并发 inject
    flow = f"STATE-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject_a", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject cores=0")
    add(flow, 1, "cpu", "rCPU_overload", "inject_b", "none", f"{DCAT} inject rCPU_overload --cores=2", 0, '"status":"ok"', "", "", "", "inject cores=2 (different resource)")
    add(flow, 2, "cpu", "rCPU_overload", "query_two", "none",
        f"{DCAT} query", 0, "", "", "out_contains:rCPU_overload", "", "two records")
    add(flow, 3, "cpu", "rCPU_overload", "clean_a", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "clean cores=0")
    add(flow, 4, "cpu", "rCPU_overload", "clean_b", "none", f"{DCAT} clean rCPU_overload --cores=2", 0, "", "", "", "", "clean cores=2")
    s_s += 1
    # S7: NPU reinject 拒绝 code5
    # 前置 setup 重置 MTU 到默认 1500，确保 inject(1280) 始终生效，
    # 避免受前序 flow（如 RES-7 clean --all 丢 state 后）残留 MTU=1280 的影响。
    flow = f"STATE-{s_s}"
    add(flow, 0, "npu", "rNPU_mtu_mismatch", "setup", "none", "hccn_tool -i {e2e_chip} -mtu -s size 1500 2>/dev/null", 0, "", "", "", "", "reset MTU baseline 1500")
    add(flow, 1, "npu", "rNPU_mtu_mismatch", "inject1", "none", f"{DCAT} inject rNPU_mtu_mismatch --chip={{e2e_chip}} --size=1280", 0, '"status":"ok"', "", "", "", "inject1 NPU")
    add(flow, 2, "npu", "rNPU_mtu_mismatch", "reject", "none", f"{DCAT} inject rNPU_mtu_mismatch --chip={{e2e_chip}} --size=1280", 5, "", "", "exitcode:5", "", "NPU reinject rejected code5")
    add(flow, 3, "npu", "rNPU_mtu_mismatch", "clean", "none", f"{DCAT} clean rNPU_mtu_mismatch --chip={{e2e_chip}}", 0, "", "", "", "", "cleanup NPU")
    s_s += 1
    # S8: batch2 新增故障 clean×2 (rMEM_leak, 无参 clean 幂等)
    flow = f"STATE-{s_s}"
    add(flow, 0, "memory", "rMEM_leak", "inject", "none", f"{DCAT} inject rMEM_leak --size_mb=32", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "memory", "rMEM_leak", "clean1", "none", f"{DCAT} clean rMEM_leak", 0, "", "ls /tmp/dcat-rMEM_leak.pid 2>/dev/null | wc -l", "==0", "", "first clean ok")
    add(flow, 2, "memory", "rMEM_leak", "clean2", "none", f"{DCAT} clean rMEM_leak", 0, "", "", "", "", "second clean: idempotent (exit 0)")
    s_s += 1
    # S9: batch2 新增故障 reinject 拒绝 code5 (rMEM_leak)
    flow = f"STATE-{s_s}"
    add(flow, 0, "memory", "rMEM_leak", "inject1", "none", f"{DCAT} inject rMEM_leak --size_mb=32", 0, '"status":"ok"', "", "", "", "inject1")
    add(flow, 1, "memory", "rMEM_leak", "reject", "none", f"{DCAT} inject rMEM_leak --size_mb=32", 5, "", "ls /tmp/dcat-rMEM_leak.pid 2>/dev/null | wc -l", ">=1", "", "reinject rejected code5")
    add(flow, 2, "memory", "rMEM_leak", "clean", "none", f"{DCAT} clean rMEM_leak", 0, "", "", "", "", "cleanup")
    s_s += 1
    # S10: batch2 新增故障 --force 替换 (rMEM_leak)
    flow = f"STATE-{s_s}"
    add(flow, 0, "memory", "rMEM_leak", "inject1", "none", f"{DCAT} inject rMEM_leak --size_mb=32", 0, '"status":"ok"', "", "", "", "inject1")
    add(flow, 1, "memory", "rMEM_leak", "force_replace", "none", f"{DCAT} inject rMEM_leak --size_mb=32 --force", 0, '"status":"ok"', "ls /tmp/dcat-rMEM_leak.pid 2>/dev/null | wc -l", ">=1", "", "--force replace")
    add(flow, 2, "memory", "rMEM_leak", "query_one", "none", f"{DCAT} query", 0, "", "", "state_contains:rMEM_leak", "", "one record")
    add(flow, 3, "memory", "rMEM_leak", "clean", "none", f"{DCAT} clean rMEM_leak", 0, "", "", "", "", "cleanup")
    s_s += 1

    # ================================================================
    # RES: 韧性/自愈 (state 丢失/损坏/孤儿/幽灵/clean --all/state 表满)
    # ================================================================
    # R1: 多故障 → 删 state → clean --all → 全清
    flow = "RES-1"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject fault1")
    add(flow, 1, "process", "rPROC_hang", "inject", "none", f"{DCAT} inject rPROC_hang --pid={{pid}}", 0, '"status":"ok"', "", "", "sleep_pid", "inject fault2")
    add(flow, 2, "mixed", "all", "lose_state", "none", "rm -f $E2E_HOME/.demoncat/state.json", 0, "", "", "", "", "simulate state deletion")
    add(flow, 3, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "one-click recovery")
    add(flow, 4, "mixed", "all", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_empty", "", "no ghost")
    # R2: state 损坏 → clean --all
    flow = "RES-2"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "mixed", "all", "corrupt_state", "none", "echo '{bad json}}}' > $E2E_HOME/.demoncat/state.json", 0, "", "", "", "", "corrupt state.json")
    add(flow, 2, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "recover despite corrupt")
    add(flow, 3, "mixed", "all", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_empty", "", "no ghost")
    # R3: 孤儿 artifact (state 删, /tmp 工件在)
    flow = "RES-3"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "mixed", "all", "orphan", "none", "rm -f $E2E_HOME/.demoncat/state.json", 0, "", "", "", "", "orphan artifact")
    add(flow, 2, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "recover orphan")
    # R4: 幽灵 state (state 有记录, /tmp 已删)
    flow = "RES-4"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "mixed", "all", "drop_artifact", "none", "rm -f /tmp/dcat-rCPU_overload-c0.pid; pkill -x perl 2>/dev/null; true", 0, "", "", "", "", "drop artifact (ghost)")
    add(flow, 2, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "", "", "", "reconcile ghost")
    add(flow, 3, "mixed", "all", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_empty", "", "ghost reconciled")
    # R5: clean --all 幂等 ×2
    flow = "RES-5"
    add(flow, 0, "mixed", "all", "clean_all_1", "none", f"{DCAT} clean --all", 0, "", "", "", "", "first clean --all")
    add(flow, 1, "mixed", "all", "clean_all_2", "none", f"{DCAT} clean --all", 0, "", "", "", "", "second clean --all (idempotent)")
    # R6: state 表满 (DCAT_MAX_RECORDS=32)
    flow = "RES-6"
    add(flow, 0, "chaos", "state_full", "fill_table", "none",
        "for p in $(seq 19000 19032); do ./build/dcat inject rNET_port_occupy --port=$p; done",
        1, "", "", 'out_contains:state table full', "", "33rd inject fails: state full")
    add(flow, 1, "chaos", "state_full", "query_records", "none",
        f"{DCAT} query", 0, "", "", "out_contains:rNET_port_occupy", "", "32 records")
    add(flow, 2, "chaos", "state_full", "clean_all", "none",
        f"{DCAT} clean --all", 0, "", "", "", "", "clean --all clears all")
    add(flow, 3, "chaos", "state_full", "query_empty", "none",
        f"{DCAT} query", 0, "", "", "state_empty", "", "empty after clean")
    # R7: NPU + CPU 多故障 → 删 state → clean --all
    flow = "RES-7"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject CPU fault")
    add(flow, 1, "npu", "rNPU_mtu_mismatch", "inject", "none", f"{DCAT} inject rNPU_mtu_mismatch --chip={{e2e_chip}} --size=1280", 0, '"status":"ok"', "", "", "", "inject NPU fault")
    add(flow, 2, "mixed", "all", "lose_state", "none", "rm -f $E2E_HOME/.demoncat/state.json", 0, "", "", "", "", "simulate state deletion")
    add(flow, 3, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "one-click recovery (CPU+NPU)")
    add(flow, 4, "mixed", "all", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_empty", "", "no ghost")

    # ================================================================
    # CLI: CLI 接口 (解析错误 + 帮助 + 退出码 + --config)
    # ================================================================
    s_cli = 1
    def cli_case(cmd, exp_code, msg_substr, behavior):
        nonlocal s_cli
        add(f"CLI-{s_cli}", 0, "cli", "cli", "negative", "none",
            cmd, exp_code, msg_substr, "", "exitcode:" + str(exp_code), "", behavior)
        s_cli += 1
    cli_case(f"{DCAT}", 2, "subcommand", "no args → help + exit2")
    cli_case(f"{DCAT} injec rCPU_overload --cores=0", 2, "missing subcommand before", "unknown subcommand")
    cli_case(f"{DCAT} inject rCPU_overload --cores=0 --force=1", 2, "--force does not take a value", "--force=x parse error")
    cli_case(f"{DCAT} inject --all rCPU_overload --cores=0", 2, "unexpected positional argument", "--all with non-clean")
    cli_case(f"{DCAT} clean", 2, "uid required", "clean no uid")
    cli_case(f"{DCAT} inject rCPU_overload --cores", 2, "missing '=value'", "--key no =value")
    cli_case(f"{DCAT} inject rCPU_overload cores=0", 2, "missing the '--' prefix", "key=value no --")
    cli_case(f"{DCAT} inject rCPU_overload --=val", 3, "unknown parameter", "--=val empty name")
    cli_case(f"{DCAT} query rCPU_overload --bogus=1", 3, "unknown parameter", "query unknown param")
    cli_case(f"{DCAT} inject rPROC_exit", 3, "missing required parameter", "inject-only missing required")
    cli_case(f"{DCAT} inject nope --cores=0", 4, "not found", "unknown uid code4")
    cli_case(f"{DCAT} inject rCPU_overload", 3, "missing required parameter", "missing required code3")
    # CLI-H: 帮助
    s_h = 1
    def help_case(cmd, msg_substr, behavior):
        nonlocal s_h
        add(f"CLI-H{s_h}", 0, "help", "help", "help", "none",
            cmd, 0, msg_substr, "", "exitcode:0", "", behavior)
        s_h += 1
    help_case(f"{DCAT} --help", "subcommand", "global help")
    help_case(f"{DCAT} inject --help", "inject <uid>", "inject subhelp")
    help_case(f"{DCAT} clean --help", "clean", "clean subhelp")
    help_case(f"{DCAT} query --help", "query", "query subhelp")
    help_case(f"{DCAT} list --help", "list", "list subhelp")
    # CLI-C: --config
    add(f"CLI-C1", 0, "config", "config", "custom", "none",
        f"{DCAT} --config config/demoncat.conf list", 0, "rCPU_overload", "", "exitcode:0", "", "--config custom path")
    add(f"CLI-C2", 0, "config", "config", "nonexistent", "none",
        f"{DCAT} --config /nonexistent/dcat.conf list", 1, "config load failed", "", "exitcode:1", "", "--config nonexistent")
    # CLI-L: list
    add(f"CLI-L1", 0, "all", "all", "list", "none",
        f"{DCAT} list", 0, 'rCPU_overload', "", "", "", "list returns catalog")

    # CLI-SERVE: HTTP serve 控制平面 (web 分支新增功能,此前零覆盖)
    # 单 flow 多步:先帮助/解析,再启动后台 serve,依次探测只读+路径穿越,最后清理
    SV_P = 18080
    sv = f"CLI-SERVE"
    def ser(step, phase, cmd, exp_code, msg_substr, vassert, behavior):
        add(sv, step, "serve", "serve", phase, "none", cmd, exp_code, msg_substr,
            "", vassert, "", behavior)
    ser(0, "help", f"{DCAT} serve --help", 0, "serve", "", "serve subhelp")
    ser(1, "badport", f"{DCAT} serve --port=abc", 2, "--port", "exitcode:2", "serve bad port parse")
    # 启动后台 serve(只读)
    ser(2, "start", f"{DCAT} serve --port {SV_P} --webroot . >/tmp/dcat_serve_e2e.log 2>&1 & sleep 1; curl -s http://127.0.0.1:{SV_P}/api/health",
        0, '"status":"ok"', 'out_contains:"status":"ok"', "serve start + health")
    # 只读:注入端点 POST → 403 (write disabled)
    ser(3, "readonly", f"curl -s -X POST http://127.0.0.1:{SV_P}/api/inject -d '{{{{\"uid\":\"rCPU_overload\"}}}}'",
        0, 'write disabled', 'out_contains:write disabled', "read-only POST inject rejected")
    # 只读查询端点健康
    ser(4, "catalog", f"curl -s http://127.0.0.1:{SV_P}/api/catalog",
        0, 'rCPU_overload', 'out_contains:rCPU_overload', "serve catalog endpoint")
    # 路径穿越防护: 字面 .. → curl 规范化后 realpath 兜底 → HTTP 404 (拒绝越界)
    ser(5, "trav_lit", f"curl -s -o /dev/null -w '%{{http_code}}' http://127.0.0.1:{SV_P}/../../etc/passwd",
        0, "", "out_contains:404", "serve traversal .. rejected (realpath 兜底)")
    # URL 编码 %2e%2e → strstr 拦截 → HTTP 403
    ser(6, "trav_enc", f"curl -s -o /dev/null -w '%{{http_code}}' 'http://127.0.0.1:{SV_P}/%2e%2e/%2e%2e/etc/passwd'",
        0, "", "out_contains:403", "serve URL-encoded traversal rejected")
    # 清理 serve 进程(用 pkill -x dcat 精确匹配进程名,避免 -f 匹配到执行 shell 自身)
    ser(7, "cleanup", "pkill -x dcat 2>/dev/null; sleep 0.5; true", 0, "", "", "kill serve")

    # ================================================================
    # CONC: 并发竞争 (新增)
    # ================================================================
    # CONC-1: inject + clean 同 uid 并发
    flow = "CONC-1"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none",
        f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "concurrent_clean_inject", "none",
        f"{DCAT} clean rCPU_overload --cores=0 & sleep 0.05; {DCAT} inject rCPU_overload --cores=0 --force", "*", "",
        "", "", "", "concurrent clean+inject: no crash")
    add(flow, 2, "cpu", "rCPU_overload", "clean", "none",
        f"{DCAT} clean rCPU_overload --cores=0", 0, "", "pgrep -x perl | wc -l", "==0", "", "cleanup")
    # CONC-2: clean --all + inject 并发
    flow = "CONC-2"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none",
        f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "concurrent_cleanall_inject", "none",
        f"{DCAT} clean --all & sleep 0.05; {DCAT} inject rCPU_overload --cores=0", "*", "",
        "", "", "", "concurrent clean --all + inject: no crash")
    add(flow, 2, "mixed", "all", "clean", "none",
        f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "cleanup")
    # CONC-3: 双 dcat 进程同时写 state
    flow = "CONC-3"
    add(flow, 0, "cpu", "rCPU_overload", "dual_inject", "none",
        f"{DCAT} inject rCPU_overload --cores=0 & {DCAT} inject rCPU_overload --cores=2 & wait", "*", "",
        "", "", "", "dual inject: no state corruption")
    add(flow, 1, "cpu", "rCPU_overload", "query_consistent", "none",
        f"{DCAT} query", 0, "", "", "state_contains:rCPU_overload", "", "state consistent")
    add(flow, 2, "mixed", "all", "clean", "none",
        f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "cleanup")

    # ================================================================
    # INTER: 故障交互 (新增)
    # ================================================================
    # INTER-1: 多故障叠加 — clean 一个不影响其他
    flow = "INTER-1"
    add(flow, 0, "cpu", "rCPU_overload", "inject_cpu", "none",
        f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject CPU")
    add(flow, 1, "network", "rNET_port_occupy", "inject_port", "none",
        f"{DCAT} inject rNET_port_occupy --port={{port}}", 0, '"status":"ok"', "", "", "free_port", "inject port")
    add(flow, 2, "cpu", "rCPU_overload", "clean_cpu", "none",
        f"{DCAT} clean rCPU_overload --cores=0", 0, "", "pgrep -x perl | wc -l", "==0", "", "clean CPU")
    add(flow, 3, "network", "rNET_port_occupy", "verify_port_still", "none",
        f"{DCAT} query", 0, "", "", "state_contains:rNET_port_occupy", "", "port still active after CPU clean")
    add(flow, 4, "network", "rNET_port_occupy", "clean_port", "none",
        f"{DCAT} clean rNET_port_occupy --port={{port}}", 0, "", "", "", "", "cleanup port")
    # INTER-2: clean --all 后逐 verify
    flow = "INTER-2"
    add(flow, 0, "cpu", "rCPU_overload", "inject_cpu", "none",
        f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject CPU")
    add(flow, 1, "network", "rNET_port_occupy", "inject_port", "none",
        f"{DCAT} inject rNET_port_occupy --port={{port}}", 0, '"status":"ok"', "", "", "free_port", "inject port")
    add(flow, 2, "mixed", "all", "clean_all", "none",
        f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "clean --all")
    add(flow, 3, "mixed", "all", "query_empty", "none",
        f"{DCAT} query", 0, "", "", "state_empty", "", "all cleared")
    # INTER-3: 同 uid 不同参数叠加
    flow = "INTER-3"
    add(flow, 0, "cpu", "rCPU_overload", "inject_a", "none",
        f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject cores=0")
    add(flow, 1, "cpu", "rCPU_overload", "inject_b", "none",
        f"{DCAT} inject rCPU_overload --cores=2", 0, '"status":"ok"', "pgrep -x perl | wc -l", ">=2", "", "inject cores=2")
    add(flow, 2, "cpu", "rCPU_overload", "clean_a", "none",
        f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "clean cores=0")
    add(flow, 3, "cpu", "rCPU_overload", "verify_b_still", "none",
        f"{DCAT} query", 0, "", "", "state_contains:rCPU_overload", "", "cores=2 still active")
    add(flow, 4, "cpu", "rCPU_overload", "clean_b", "none",
        f"{DCAT} clean rCPU_overload --cores=2", 0, "", "pgrep -x perl | wc -l", "==0", "", "clean cores=2")

    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "cases.csv"))
    args = ap.parse_args()
    rows = gen()
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS, lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") for k in COLUMNS})
    print(f"generated {len(rows)} cases -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
