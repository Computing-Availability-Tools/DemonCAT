#!/usr/bin/env python3
"""tests/e2e/gen_cases.py — 从 config/demoncat.conf 故障目录 + 内置观测/边界/安全知识
自动生成 tests/e2e/cases.csv。

矩阵：
  F 功能基线  : 37 故障 × (setup→inject+verify→clean+verify→query无幽灵)
  B 边界值    : 每参数类型 valid-edge + invalid（期望 code3 或脚本 exit1）
  H 主机安全  : 危险资源无守卫确认 + 路径穿越
  P 权限边界  : 非 root 跑 root 故障 → 优雅失败 + 无半成品
  I 命令注入  : 每参数塞良性 shell 元字符载荷 → 未执行
  R 自愈恢复  : state 删除/损坏/kill-9/孤儿/幽灵/幂等
  S 一致性幂等: clean×2 / --force×2 / query×2 / 并发 / clean 后系统复位

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
    "verify_cmd", "verify_assert", "provision", "expected_behavior",
]

# ---- 每故障观测知识（verify 用 linux 命令确认 dcat 真的达到效果） ----
# 字段: module, inject_args, clean_args, provision, precondition,
#       v_cmd/v_assert(注入后观测), c_cmd/c_assert(清除后观测)
OBS = {
    # ---- 非root (CI/WSL 可跑) ----
    "rCPU_overload": dict(module="cpu", inject_args="--cores=0 --load_pct=100",
        clean_args="--cores=0", provision="none", precondition="none",
        v_cmd="pgrep -x perl | wc -l", v_assert=">=1",
        c_cmd="pgrep -x perl | wc -l", c_assert="==0"),
    "rCPU_core_offline": dict(module="cpu", inject_args="--cores=1",
        clean_args="--cores=1", provision="none", precondition="root+sysfs_writable+allow_cpu_offline",
        v_cmd="cat /sys/devices/system/cpu/cpu1/online 2>/dev/null || echo NA", v_assert="eq:0",
        c_cmd="cat /sys/devices/system/cpu/cpu1/online 2>/dev/null || echo NA", c_assert="eq:1"),
    "rDISK_write_overload": dict(module="storage", inject_args="--device=/tmp --workers=2 --size_mb=200",
        clean_args="--device=/tmp", provision="none", precondition="none",
        v_cmd="ls /tmp/dcat.stress.* 2>/dev/null | wc -l", v_assert=">=1",
        c_cmd="ls /tmp/dcat.stress.* 2>/dev/null | wc -l", c_assert="==0"),
    # ---- 网络 ----
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
        clean_args="--iface={iface}", provision="real_phy", precondition="root+ethtool+real_phy",
        v_cmd="ethtool {iface} 2>/dev/null | grep -oE 'Speed: [0-9]+[^ ]*'", v_assert="regex:Speed: 10[^0-9]",
        c_cmd="ethtool {iface} 2>/dev/null | grep -oE 'Speed: [0-9]+[^ ]*'", c_assert="nonempty"),
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
    # ---- 进程 ----
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
    # ---- NPU (无硬件自动 skip) ----
    "rNPU_link_down": dict(module="npu", inject_args="--chip=0", clean_args="--chip=0",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -link -g 2>/dev/null", v_assert="contains:down",
        c_cmd="hccn_tool -i 0 -link -g 2>/dev/null", c_assert="notcontains:down"),
    "rNPU_ip_change": dict(module="npu", inject_args="--chip=0 --address=10.0.0.99 --netmask=255.255.255.0",
        clean_args="--chip=0", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -ip -g 2>/dev/null", v_assert="contains:10.0.0.99",
        c_cmd="hccn_tool -i 0 -ip -g 2>/dev/null", c_assert="notcontains:10.0.0.99"),
    "rNPU_gw_change": dict(module="npu", inject_args="--chip=0 --gateway=10.0.0.250",
        clean_args="--chip=0", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -gateway -g 2>/dev/null", v_assert="contains:10.0.0.250",
        c_cmd="hccn_tool -i 0 -gateway -g 2>/dev/null", c_assert="notcontains:10.0.0.250"),
    "rNPU_netdetect_change": dict(module="npu", inject_args="--chip=0 --address=10.0.0.99",
        clean_args="--chip=0", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -netdetect -g 2>/dev/null", v_assert="contains:10.0.0.99",
        c_cmd="hccn_tool -i 0 -netdetect -g 2>/dev/null", c_assert="notcontains:10.0.0.99"),
    "rNPU_arp_poison": dict(module="npu", inject_args="--chip=0 --dev=eth0 --ip=10.0.0.99 --mac=de:ad:be:ef:00:99",
        clean_args="--chip=0 --dev=eth0 --ip=10.0.0.99", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -arp -g 2>/dev/null", v_assert="contains:de:ad:be:ef:00:99",
        c_cmd="hccn_tool -i 0 -arp -g 2>/dev/null", c_assert="notcontains:de:ad:be:ef:00:99"),
    "rNPU_arp_del": dict(module="npu", inject_args="--chip=0 --dev=eth0 --ip=10.0.0.99",
        clean_args="--chip=0 --dev=eth0 --ip=10.0.0.99", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -arp -g 2>/dev/null", v_assert="notcontains:10.0.0.99",
        c_cmd="hccn_tool -i 0 -arp -g 2>/dev/null", c_assert="contains:10.0.0.99"),
    "rNPU_route_add": dict(module="npu", inject_args="--chip=0 --address=10.1.0.0 --netmask=255.255.0.0 --gateway=10.0.0.1",
        clean_args="--chip=0 --address=10.1.0.0 --netmask=255.255.0.0", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -route -g 2>/dev/null", v_assert="contains:10.1.0.0",
        c_cmd="hccn_tool -i 0 -route -g 2>/dev/null", c_assert="notcontains:10.1.0.0"),
    "rNPU_route_del": dict(module="npu", inject_args="--chip=0 --address=10.1.0.0 --netmask=255.255.0.0",
        clean_args="--chip=0 --address=10.1.0.0 --netmask=255.255.0.0", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -route -g 2>/dev/null", v_assert="notcontains:10.1.0.0",
        c_cmd="hccn_tool -i 0 -route -g 2>/dev/null", c_assert="contains:10.1.0.0"),
    "rNPU_route_clear": dict(module="npu", inject_args="--chip=0", clean_args="--chip=0",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -route -g 2>/dev/null | grep -cE 'address|gateway'", v_assert="==0",
        c_cmd="hccn_tool -i 0 -route -g 2>/dev/null | grep -cE 'address|gateway'", c_assert=">=1"),
    "rNPU_iprule_add": dict(module="npu", inject_args="--chip=0 --dir=from --ip=192.168.1.100 --table=100",
        clean_args="--chip=0 --dir=from --ip=192.168.1.100", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -ip_rule -g 2>/dev/null", v_assert="contains:192.168.1.100",
        c_cmd="hccn_tool -i 0 -ip_rule -g 2>/dev/null", c_assert="notcontains:192.168.1.100"),
    "rNPU_iprule_del": dict(module="npu", inject_args="--chip=0 --dir=from --ip=192.168.1.100",
        clean_args="--chip=0 --dir=from --ip=192.168.1.100", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -ip_rule -g 2>/dev/null", v_assert="notcontains:192.168.1.100",
        c_cmd="hccn_tool -i 0 -ip_rule -g 2>/dev/null", c_assert="contains:192.168.1.100"),
    "rNPU_iproute_add": dict(module="npu", inject_args="--chip=0 --ip=10.2.0.0 --ip_mask=255.255.0.0 --via=10.0.0.1 --dev=eth0 --table=100",
        clean_args="--chip=0 --ip=10.2.0.0 --ip_mask=255.255.0.0 --table=100", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -ip_route -g table 100 2>/dev/null", v_assert="contains:10.2.0.0",
        c_cmd="hccn_tool -i 0 -ip_route -g table 100 2>/dev/null", c_assert="notcontains:10.2.0.0"),
    "rNPU_iproute_del": dict(module="npu", inject_args="--chip=0 --ip=10.2.0.0 --ip_mask=255.255.0.0 --table=100",
        clean_args="--chip=0 --ip=10.2.0.0 --ip_mask=255.255.0.0 --table=100", provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -ip_route -g table 100 2>/dev/null", v_assert="notcontains:10.2.0.0",
        c_cmd="hccn_tool -i 0 -ip_route -g table 100 2>/dev/null", c_assert="contains:10.2.0.0"),
    "rNPU_bw_limit": dict(module="npu", inject_args="--chip=0 --bw_limit=10000", clean_args="--chip=0",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -shaping -g 2>/dev/null | grep -oE 'bw_limit [0-9]+'", v_assert="contains:10000",
        c_cmd="hccn_tool -i 0 -shaping -g 2>/dev/null | grep -oE 'bw_limit [0-9]+'", c_assert="notcontains:10000"),
    "rNPU_mtu_mismatch": dict(module="npu", inject_args="--chip=0 --size=1280", clean_args="--chip=0",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -mtu -g 2>/dev/null | grep -oE 'mtu [0-9]+'", v_assert="contains:1280",
        c_cmd="hccn_tool -i 0 -mtu -g 2>/dev/null | grep -oE 'mtu [0-9]+'", c_assert="notcontains:1280"),
    "rNPU_fec_change": dict(module="npu", inject_args="--chip=0 --encoding=rs", clean_args="--chip=0",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -fec -g 2>/dev/null", v_assert="contains:rs",
        c_cmd="hccn_tool -i 0 -fec -g 2>/dev/null", c_assert="contains:rs"),
    "rNPU_dscp_tc_change": dict(module="npu", inject_args="--chip=0 --dscp=46 --tc=0", clean_args="--chip=0 --dscp=46",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -dscp_to_tc -g dscp 46 2>/dev/null", v_assert="contains:tc 0",
        c_cmd="hccn_tool -i 0 -dscp_to_tc -g dscp 46 2>/dev/null", c_assert="notcontains:tc 0"),
    "rNPU_prio_tc_change": dict(module="npu", inject_args="--chip=0 --map=0,0,0,0,0,0,0,0", clean_args="--chip=0",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -prio_tc -g 2>/dev/null | grep -oE 'map [0-9,]+'", v_assert="contains:0,0,0,0,0,0,0,0",
        c_cmd="hccn_tool -i 0 -prio_tc -g 2>/dev/null | grep -oE 'map [0-9,]+'", c_assert="nonempty"),
    "rNPU_pfc_change": dict(module="npu", inject_args="--chip=0 --bitmap=1,1,1,1,1,1,1,1", clean_args="--chip=0",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -pfc -g 2>/dev/null | grep -oE 'bitmap [0-9,]+'", v_assert="contains:1,1,1,1,1,1,1,1",
        c_cmd="hccn_tool -i 0 -pfc -g 2>/dev/null | grep -oE 'bitmap [0-9,]+'", c_assert="nonempty"),
    "rNPU_roce_port_change": dict(module="npu", inject_args="--chip=0 --port=4791", clean_args="--chip=0",
        provision="none", precondition="hccn_tool",
        v_cmd="hccn_tool -i 0 -udp -g 2>/dev/null", v_assert="contains:4791",
        c_cmd="hccn_tool -i 0 -udp -g 2>/dev/null", c_assert="contains:4791"),
}

DCAT = "./build/dcat"  # 框架从项目根跑


def load_catalog():
    cp = configparser.ConfigParser()
    cp.read(CONF)
    uids = []
    for sec in cp.sections():
        if sec.startswith("fault."):
            uid = sec[len("fault."):]
            ops = cp.get(sec, "supported_ops", fallback="")
            uids.append((uid, ops))
    return uids, cp


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
            "expected_behavior": behavior,
        })

    # ===== F 功能基线 =====
    for uid in sorted(OBS):
        o = OBS[uid]
        flow = f"F-{uid}"
        s = 0
        # setup
        if o.get("provision", "none") != "none":
            add(flow, s, o["module"], uid, "setup", o["precondition"], "", 0, "",
                "", "", o["provision"], "provision " + o["provision"]); s += 1
        # inject + verify
        add(flow, s, o["module"], uid, "inject", o["precondition"],
            f"{DCAT} inject {uid} {o['inject_args']}", 0, '"status":"ok"',
            o["v_cmd"], o["v_assert"], "", "fault active after inject"); s += 1
        if not o.get("inject_only"):
            add(flow, s, o["module"], uid, "clean", o["precondition"],
                f"{DCAT} clean {uid} {o['clean_args']}", 0, "", o["c_cmd"], o["c_assert"],
                "", "fault cleaned, system restored"); s += 1
            add(flow, s, o["module"], uid, "query", o["precondition"],
                f"{DCAT} query", 0, "", "", f"state_not_contains:{uid}", "",
                "no ghost record after clean"); s += 1
        else:
            # inject-only: clean/query 应被拒绝 (code 3)
            add(flow, s, o["module"], uid, "clean_rejected", o["precondition"],
                f"{DCAT} clean {uid} {o['inject_args']}", 3, "", "", "exitcode:3",
                "", "inject-only: clean rejected"); s += 1

    # ===== B 边界值（实测 exit code；rejected=无副作用；gap-accepted 需 clean） =====
    s_b = 1

    def b_reject(uid, args, exp_code, exp_json, behavior, pre="none"):
        nonlocal s_b
        flow = f"B-{s_b}"
        add(flow, 0, OBS[uid]["module"], uid, "inject", pre,
            f"{DCAT} inject {uid} {args}", exp_code, exp_json, "", "exitcode:" + str(exp_code), "", behavior)
        s_b += 1

    def b_gap(uid, args, clean_args, behavior, pre="none"):
        nonlocal s_b
        flow = f"B-{s_b}"
        add(flow, 0, OBS[uid]["module"], uid, "inject", pre,
            f"{DCAT} inject {uid} {args}", 0, '"status":"ok"', "", "", "", behavior + " (GAP: accepted)")
        add(flow, 1, OBS[uid]["module"], uid, "clean", pre,
            f"{DCAT} clean {uid} {clean_args}", 0, "", "", "", "", "cleanup gap case")
        s_b += 1

    # cores: rejected by script (code 1)
    b_reject("rCPU_overload", "--cores=0/1", 1, 'invalid cores spec', "cores invalid separator /")
    b_reject("rCPU_overload", "--cores=abc", 1, 'invalid cores spec', "cores non-numeric")
    # cores: rejected by precheck (code 3)
    b_reject("rCPU_overload", "--cores=", 3, 'missing required parameter', "empty cores = missing")
    b_reject("rCPU_overload", "", 3, 'missing required parameter', "missing cores")
    # cores: gap-accepted (shell parse_cores 无 range/order/trailing 校验)
    b_gap("rCPU_overload", "--cores=0-999", "--cores=0-999", "0-999 accepted (no range cap)")
    b_gap("rCPU_overload", "--cores=3-1", "--cores=3-1", "3-1 accepted (lo>hi, 0 cores)")
    b_gap("rCPU_overload", "--cores=0,1,", "--cores=0,1,", "trailing comma accepted")
    # load_pct: script rejects (code 1) — 校验在注入 perl 前，无副作用
    b_reject("rCPU_overload", "--cores=0 --load_pct=0", 1, 'load_pct must be', "load_pct=0")
    b_reject("rCPU_overload", "--cores=0 --load_pct=500", 1, 'load_pct must be', "load_pct=500")
    b_reject("rCPU_overload", "--cores=0 --load_pct=-1", 1, 'load_pct must be', "load_pct=-1")
    b_reject("rCPU_overload", "--cores=0 --load_pct=abc", 1, 'load_pct must be', "load_pct non-numeric")
    # port: gap-accepted (无 numeric/range 校验)
    b_gap("rNET_port_occupy", "--port=abc", "--port=abc", "port=abc accepted (no numeric validation)")
    b_gap("rNET_port_occupy", "--port=65536", "--port=65536", "port=65536 accepted (no range validation)")
    b_gap("rNET_port_occupy", "--port=0", "--port=0", "port=0 accepted (binds random)")
    # chip: script npu_validate_chip 拒绝 (code 1, 无需 hccn_tool)
    b_reject("rNPU_fec_change", "--chip=10 --encoding=rs", 1, 'chip must be a single digit', "chip=10 (2 digits)")
    b_reject("rNPU_fec_change", "--chip=a --encoding=rs", 1, 'chip must be a single digit', "chip=a (non-digit)")
    b_reject("rNPU_fec_change", "--encoding=rs", 3, 'missing required parameter', "chip missing")

    # ===== I 命令注入（良性载荷 touch /tmp/dcat_pwned；只验 notexists，exit code 不care） =====
    PAYLOADS = [";touch /tmp/dcat_pwned", "$(touch /tmp/dcat_pwned)", "|touch /tmp/dcat_pwned"]
    INJ_TARGETS = [
        ("rNET_down", "--iface=dcat-e2e0{x}", "root+ip+dummy_iface"),
        ("rNET_port_occupy", "--port=19999{x}", "none"),
        ("rCPU_overload", "--cores=0{x}", "none"),
        ("rDISK_write_overload", "--device=/tmp{x}", "none"),
        ("rNET_service_stop", "--service=cron{x}", "root+systemd+noncritical_svc"),
        ("rNPU_fec_change", "--chip=0{x} --encoding=rs", "hccn_tool"),
        ("rNPU_ip_change", "--chip=0 --address=1.1.1.1{x} --netmask=255.255.255.0", "hccn_tool"),
    ]
    s_i = 1
    for uid, argtmpl, pre in INJ_TARGETS:
        for p in PAYLOADS:
            flow = f"I-{s_i}"
            args = argtmpl.replace("{x}", p)
            add(flow, 0, OBS[uid]["module"], uid, "inject", pre,
                f"{DCAT} inject {uid} {args}", "*", "",
                "test -e /tmp/dcat_pwned && echo FOUND || echo CLEAN", "eq:CLEAN", "",
                f"payload not executed: {p}")
            s_i += 1

    # ===== P 权限边界（非root 跑 root 故障 → 失败 + 无半成品） =====
    s_p = 1

    def p_case(uid, args, pre, vcmd, vassert, behavior):
        nonlocal s_p
        flow = f"P-{s_p}"
        add(flow, 0, OBS[uid]["module"], uid, "inject", pre,
            f"{DCAT} inject {uid} {args}", "nonzero", "", vcmd, vassert, "", behavior)
        add(flow, 1, OBS[uid]["module"], uid, "query_noghost", pre,
            f"{DCAT} query", 0, "", "", f"state_not_contains:{uid}", "", "no state ghost on perm fail")
        s_p += 1
    p_case("rNET_delay", "--iface=dcat-e2e0 --delay_ms=100", "non_root",
           "tc qdisc show dev dcat-e2e0 2>/dev/null | grep -c netem", "==0", "non-root: no qdisc added")
    p_case("rNET_tcp_loss", "--port=19998", "non_root",
           "iptables -L INPUT -n 2>/dev/null | grep -c 'dpt:19998'", "==0", "non-root: no iptables rule")

    # ===== H 主机安全 =====
    s_h = 1
    # H1: rNET_down 不拒绝管理网卡名（无守卫）— 用不存在的 iface，dcat 越过 precheck 在脚本失败
    add(f"H-{s_h}", 0, "network", "rNET_down", "inject", "none",
        f"{DCAT} inject rNET_down --iface=eth0-mgmt-test", "nonzero", "",
        "", "notcontains:not allowed", "", "no mgmt-iface guard: dcat proceeds past precheck (risk)"); s_h += 1
    # H2: rNET_service_stop 不拒绝 sshd（无守卫）— 用不存在的服务名
    add(f"H-{s_h}", 0, "network", "rNET_service_stop", "inject", "none",
        f"{DCAT} inject rNET_service_stop --service=sshd-test", "nonzero", "",
        "", "notcontains:not allowed", "", "no sshd guard: dcat accepts service name (risk)"); s_h += 1
    # H3: 写入边界（安全路径）— device=/tmp，验证 dcat 只写 /tmp、不污染 /etc（root 也安全）
    h3 = f"H-{s_h}"
    add(h3, 0, "storage", "rDISK_write_overload", "inject", "none",
        f"{DCAT} inject rDISK_write_overload --device=/tmp --workers=2 --size_mb=200", 0, '"status":"ok"',
        "ls /etc/dcat.stress.* /etc/dcat.write.* 2>/dev/null | wc -l", "==0", "",
        "write containment: /tmp written, /etc not polluted")
    add(h3, 1, "storage", "rDISK_write_overload", "clean", "none",
        f"{DCAT} clean rDISK_write_overload --device=/tmp", 0, "", "", "", "", "cleanup H-3")
    s_h += 1
    # H4: 未知参数拒绝
    add(f"H-{s_h}", 0, "cpu", "rCPU_overload", "inject", "none",
        f"{DCAT} inject rCPU_overload --cores=0 --bogus=1", 3, 'unknown parameter', "", "exitcode:3",
        "", "undeclared param rejected"); s_h += 1

    # ===== R 自愈与一键恢复 =====
    s_r = 1
    # R1: 多故障注入 → 删 state → clean --all → 全清
    flow = f"R-{s_r}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject fault1"); 
    add(flow, 1, "process", "rPROC_hang", "inject", "none", f"{DCAT} inject rPROC_hang --pid={{pid}}", 0, '"status":"ok"', "", "", "sleep_pid", "inject fault2")
    add(flow, 2, "mixed", "all", "lose_state", "none", "rm -f $E2E_HOME/.demoncat/state.json", 0, "", "", "", "", "simulate state deletion")
    add(flow, 3, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "one-click recovery: all faults cleared")
    add(flow, 4, "mixed", "all", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_empty", "", "no ghost after recovery")
    s_r += 1
    # R2: state 损坏 → clean --all
    flow = f"R-{s_r}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "mixed", "all", "corrupt_state", "none", "echo '{not valid json}}}' > $E2E_HOME/.demoncat/state.json", 0, "", "", "", "", "corrupt state.json")
    add(flow, 2, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "recover despite corrupt state")
    add(flow, 3, "mixed", "all", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_empty", "", "no ghost")
    s_r += 1
    # R3: 注入中途 kill -9 dcat（模拟）→ clean --all 恢复
    flow = f"R-{s_r}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "mixed", "all", "orphan_artifact", "none", "rm -f $E2E_HOME/.demoncat/state.json", 0, "", "", "", "", "leave /tmp artifact, drop state (orphan)")
    add(flow, 2, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "pgrep -x perl | wc -l", "==0", "", "recover orphan artifact")
    s_r += 1
    # R4: 幽灵 state（state 有记录 /tmp 已删）→ clean --all reconcile
    flow = f"R-{s_r}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "mixed", "all", "drop_artifact", "none", "rm -f /tmp/dcat-rCPU_overload-c0.pid; pkill -x perl 2>/dev/null; true", 0, "", "", "", "", "manually drop /tmp artifact (ghost state)")
    add(flow, 2, "mixed", "all", "clean_all", "none", f"{DCAT} clean --all", 0, "", "", "", "", "clean --all reconciles ghost")
    add(flow, 3, "mixed", "all", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_empty", "", "ghost reconciled")
    s_r += 1
    # R5: clean --all 幂等 ×2
    flow = f"R-{s_r}"
    add(flow, 0, "mixed", "all", "clean_all_1", "none", f"{DCAT} clean --all", 0, "", "", "", "", "first clean --all")
    add(flow, 1, "mixed", "all", "clean_all_2", "none", f"{DCAT} clean --all", 0, "", "", "", "", "second clean --all (idempotent)")
    s_r += 1

    # ===== S 状态一致性与幂等性 =====
    s_s = 1
    # S1: clean --params ×2 → 第二次 code1 no-active-injection
    flow = f"S-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "clean1", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "pgrep -x perl | wc -l", "==0", "", "first clean ok")
    add(flow, 2, "cpu", "rCPU_overload", "clean2", "none", f"{DCAT} clean rCPU_overload --cores=0", 1, "", "", "", "", "second clean: no active injection (idempotent)")
    s_s += 1
    # S2: 无参 clean ×2 → 第二次 exit0 no-active
    flow = f"S-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "clean1", "none", f"{DCAT} clean rCPU_overload", 0, "", "pgrep -x perl | wc -l", "==0", "", "no-arg clean ok")
    add(flow, 2, "cpu", "rCPU_overload", "clean2", "none", f"{DCAT} clean rCPU_overload", 0, "", "", "", "", "no-arg clean idempotent")
    add(flow, 3, "cpu", "rCPU_overload", "query_empty", "none", f"{DCAT} query", 0, "", "", "state_not_contains:rCPU_overload", "", "no ghost")
    s_s += 1
    # S3: --force ×2 → 仅 1 active
    flow = f"S-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject1", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject1")
    add(flow, 1, "cpu", "rCPU_overload", "force_replace", "none", f"{DCAT} inject rCPU_overload --cores=0 --force", 0, '"status":"ok"', "pgrep -x perl | wc -l", "<=2", "", "--force replace (not double)")
    add(flow, 2, "cpu", "rCPU_overload", "query_one", "none", f"{DCAT} query", 0, "", "", "state_contains:rCPU_overload", "", "exactly one record")
    add(flow, 3, "cpu", "rCPU_overload", "clean", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "cleanup")
    s_s += 1
    # S4: 重注入默认拒绝 code5
    flow = f"S-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject1", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject1")
    add(flow, 1, "cpu", "rCPU_overload", "reject", "none", f"{DCAT} inject rCPU_overload --cores=0", 5, "", "pgrep -x perl | wc -l", ">=1", "", "reinject rejected code5, old still active")
    add(flow, 2, "cpu", "rCPU_overload", "clean", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "cleanup")
    s_s += 1
    # S5: query ×2 一致
    flow = f"S-{s_s}"
    add(flow, 0, "cpu", "rCPU_overload", "inject", "none", f"{DCAT} inject rCPU_overload --cores=0", 0, '"status":"ok"', "", "", "", "inject")
    add(flow, 1, "cpu", "rCPU_overload", "query1", "none", f"{DCAT} query", 0, "", "", "state_contains:rCPU_overload", "", "query sees it")
    add(flow, 2, "cpu", "rCPU_overload", "query2", "none", f"{DCAT} query", 0, "", "", "state_contains:rCPU_overload", "", "query idempotent")
    add(flow, 3, "cpu", "rCPU_overload", "clean", "none", f"{DCAT} clean rCPU_overload --cores=0", 0, "", "", "", "", "cleanup")
    s_s += 1

    # 基本面：list / help
    add("MISC-1", 0, "all", "all", "list", "none", f"{DCAT} list", 0, 'rCPU_overload', "", "", "", "list returns catalog")
    add("MISC-2", 0, "all", "all", "help", "none", f"{DCAT} --help", 0, 'subcommand', "", "", "", "help works")
    add("MISC-3", 0, "all", "nope", "inject", "none", f"{DCAT} inject nope --cores=0", 4, 'not found', "", "exitcode:4", "", "unknown uid code4")
    add("MISC-4", 0, "all", "rCPU_overload", "inject_missing", "none", f"{DCAT} inject rCPU_overload", 3, 'missing required parameter', "", "exitcode:3", "", "missing required code3")

    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "cases.csv"))
    args = ap.parse_args()
    rows = gen()
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") for k in COLUMNS})
    print(f"generated {len(rows)} cases -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
