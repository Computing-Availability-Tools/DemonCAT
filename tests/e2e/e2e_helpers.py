#!/usr/bin/env python3
"""tests/e2e/e2e_helpers.py — dcat e2e 环境/执行 helper（从 run_e2e.py 抽取，供 pytest 复用）。

提供：shell 执行（sh/sh_sep/run_step_cmd，dcat 用 argv 防注入假阳性）、
幂等环境清扫（sweep/SWEEP_SCRIPT）、资源 provision（sleep_pid/free_port/dummy_iface/
real_phy/noncritical_svc）、占位符替换（substitute）、物理前置检查（check_precondition）、
JSON 状态解析（json_state_data）、基础断言算子（apply_assert）。
由 tests/e2e/conftest.py 与 e2e_assert.py import 复用，零行为改动。
"""
import argparse
import csv
import json
import os
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import time
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))   # tests/e2e
ROOT = os.path.dirname(os.path.dirname(HERE))        # project root
DCAT = os.path.join(ROOT, "build", "dcat")
CASES = os.path.join(HERE, "cases.csv")
_worker_id = os.environ.get("PYTEST_XDIST_WORKER", "")
if _worker_id:
    _wn = _worker_id.replace("gw", "")
    TEST_IFACE = f"dcat-e2e{_wn}"
    E2E_HOME = f"/tmp/dcat_e2e_home_{_worker_id}"
else:
    TEST_IFACE = "dcat-e2e0"
    E2E_HOME = "/tmp/dcat_e2e_home"
PWN = "/tmp/dcat_pwned"

# 测试分类说明（分类→测试目的概述），供 report.md / test_report.md 复用
CAT_DESC = {
    "FUNC": "功能基线 — 故障 inject→verify→clean→query 全链路 + query<uid> 状态确认 + 插件生命周期",
    "BOUND": "边界值 — 每参数类型系统性覆盖（整数越界/空值/格式错误/枚举非法）",
    "SEC": "安全 — 命令注入(inject+clean+query) + 权限边界(非root拒绝) + 主机安全(路径穿越/symlink)",
    "STATE": "状态一致性 — clean×2幂等 / --force替换 / 重注入拒绝 / query幂等 / 多资源隔离",
    "RES": "韧性/自愈 — state丢失/损坏/孤儿/幽灵恢复 / clean--all幂等 / state表满",
    "CLI": "CLI接口 — 解析错误 + 帮助 + 退出码 + --config + HTTP serve控制平面",
    "CONC": "并发竞争 — 同时inject+clean / 双进程写state / clean--all+inject",
    "INTER": "故障交互 — 多故障叠加 / clean一个不影响其他 / clean--all后逐verify",
}

RESULT_COLS = [
    "id", "flow_id", "step", "phase", "fault_uid", "module",
    "command", "expected_exit_code", "expected_json", "verify_cmd", "verify_assert",
    "expected_behavior",
    "actual_exit_code", "actual_json", "verify_actual", "result", "error_code",
    "duration_ms", "timestamp", "notes",
]


def sh(cmd, env=None, timeout=60):
    """run shell cmd, return (rc, out). Safe-ish (shell=True)."""
    try:
        p = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                           env=env, timeout=timeout)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except subprocess.TimeoutExpired:
        return 124, "[timeout]"
    except Exception as e:
        return 1, f"[exception {e}]"


def sh_sep(cmd, env=None, timeout=60, cwd=None):
    """run shell cmd, return (rc, stdout, stderr) separately for failure diagnostics."""
    try:
        p = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                           env=env, timeout=timeout, cwd=cwd)
        return p.returncode, (p.stdout or ""), (p.stderr or "")
    except subprocess.TimeoutExpired:
        return 124, "", "[timeout]"
    except Exception as e:
        return 1, "", f"[exception {e}]"


def run_step_cmd(cmd, env, dcat_bin, timeout=120, priv_user=None):
    """dcat 命令用 argv 列表执行（不带 shell），使注入载荷原样进入 dcat cli_parse
    （否则 shell=True 会把 ';touch ...' 当命令分隔符，框架自身执行载荷=假阳性）。
    但 CONC 测试用 & wait 做并发，必须 shell=True。
    辅助 shell 命令(rm/echo/pkill/for/$VAR) 仍用 shell=True。
    priv_user: 非 None 时用 runuser 降权到该用户执行（P 类验证非 root 拒绝）。
    dcat_bin: --dcat 指定的实际二进制；与 cases.csv 默认 ./build/dcat 不一致时替换。
    返回 (rc, stdout, stderr) 三元组，便于失败时分别记录诊断信息。"""
    cs = cmd.strip()
    default_rel = "./build/dcat"
    if dcat_bin != default_rel:
        cs = cs.replace(default_rel, dcat_bin)
    dcat_rel = dcat_bin
    is_nonroot = hasattr(os, "geteuid") and os.geteuid() != 0
    auto_sudo = is_nonroot and os.environ.get("DCAT_AUTO_SUDO") == "1"
    # CONC 测试含 & wait、SEC-S1 clean 含 ; rm 需要 shell；
    # SEC-I 注入含 ;touch（无空格）必须用 argv 防止载荷执行
    needs_shell = ('&' in cs and 'wait' in cs) or ('; ' in cs)
    if (cs.startswith(DCAT) or cs.startswith(dcat_rel)) and not needs_shell:
        try:
            argv = shlex.split(cs)
            if priv_user:
                # runuser -u <user> -- <argv>；root 专用，免密
                if sh(f"command -v runuser >/dev/null 2>&1")[0] != 0:
                    return 1, "", "[runuser 未安装，无法降权验证非 root 拒绝]"
                argv = ["runuser", "-u", priv_user, "--"] + argv
            elif auto_sudo:
                # sudo 默认 env_reset + always_set_home 会把 HOME 重置为 /root，
                # 导致 dcat 的 state.json 写进 /root/.demoncat，而 sweep 只清 E2E_HOME
                # → 前序用例状态残留 → 重注入返回 exit 5（resource already injected）。
                # 显式 HOME=E2E_HOME 让 state 落在 sweep 能清理的隔离目录。
                argv = ["sudo", "-n", "-E", "env", f"HOME={E2E_HOME}"] + argv
            p = subprocess.run(argv, capture_output=True, text=True, env=env,
                               timeout=timeout, cwd=ROOT)
            return p.returncode, (p.stdout or ""), (p.stderr or "")
        except subprocess.TimeoutExpired:
            return 124, "", "[timeout]"
        except Exception as e:
            return 1, "", f"[exception {e}]"
    # 非 dcat 命令（verify 等）：auto_sudo 时加 sudo 前缀
    # iptables -L / cat /sys/... 等 verify 命令需要 root 权限
    if auto_sudo and not (cs.startswith(DCAT) or cs.startswith(dcat_rel)):
        cs = f"sudo -n -E {cs}"
    # needs_shell 的 dcat 命令（含 & wait / ; 的 CONC 测试）也是 dcat 进程：
    # auto_sudo 下必须同样加 sudo + HOME 注入，否则非 root 裸跑注入失败且难排查
    if auto_sudo and needs_shell and (cs.startswith(DCAT) or cs.startswith(dcat_rel)):
        cs = f"sudo -n -E env HOME={E2E_HOME} {cs}"
    return sh_sep(cs, env=env, timeout=timeout)


def cmd_exists(c):
    return sh(f"command -v {c} >/dev/null 2>&1")[0] == 0


# ---------------- 环境清扫（dcat 命名空间内，对宿主安全） ----------------
SWEEP_SCRIPT = r'''
set +e
# Kill dcat-spawned processes (child processes survive parent shell kill)
pkill -f 'perl -e' 2>/dev/null
pkill -x yes 2>/dev/null
pkill -9 -f 'dd if=/dev/zero of=.*dcat' 2>/dev/null
sleep 0.5
pkill -9 -f 'dd if=/dev/zero of=.*dcat' 2>/dev/null
# rNET_port_occupy: python3 socket bind listen (survives parent shell)
pkill -f 'python3 -c.*socket.*bind.*listen' 2>/dev/null
# rNET_conn_exhaust: python3 create_connection
pkill -f 'python3 -c.*create_connection' 2>/dev/null
# rNET_service_stop: pkill fallback
pkill -f 'dcat-rNET_port_occupy' 2>/dev/null
# rNET_link_flap: kill background subshell from PIDFILEs
for pf in /tmp/dcat-rNET_link_flap-*.pid; do
    [ -f "$pf" ] || continue
    kill "$(cat "$pf" 2>/dev/null)" 2>/dev/null
    rm -f "$pf"
done
pkill -f 'ip link set.*down' 2>/dev/null
pkill -f 'ip link set.*up' 2>/dev/null
# rNPU compute load: kill _npu_stress processes (pidfiles deleted below, processes survive)
for pf in /tmp/dcat-rNPU_aic_load-*.pid /tmp/dcat-rNPU_aicpu_load-*.pid /tmp/dcat-rNPU_aiv_load-*.pid /tmp/dcat-rNPU_hbm_load-*.pid; do
    [ -f "$pf" ] || continue
    while read -r p; do
        [ -n "$p" ] && kill -9 "$p" 2>/dev/null
    done < "$pf"
done
pkill -9 -f '_npu_stress' 2>/dev/null
# NPU stale state cleanup (before rm /tmp/dcat-* — guard checks sidecar files exist)
if command -v hccn_tool >/dev/null 2>&1 && ls /tmp/dcat-rNPU_* >/dev/null 2>&1; then
  for c in 2 5; do
    hccn_tool -i $c -ip_rule -d dir from ip 10.20.10.210 2>/dev/null
    hccn_tool -i $c -ip_rule -d dir from ip 10.20.10.211 2>/dev/null
    hccn_tool -i $c -route -d address 10.30.40.0 netmask 255.255.255.0 2>/dev/null
    hccn_tool -i $c -route -d address 10.30.41.0 netmask 255.255.255.0 2>/dev/null
    hccn_tool -i $c -ip_route -d ip 10.30.50.0 ip_mask 24 table 100 2>/dev/null
    hccn_tool -i $c -ip_route -d ip 10.30.51.0 ip_mask 24 table 100 2>/dev/null
    hccn_tool -i $c -link -s up 2>/dev/null
    hccn_tool -i $c -shaping -s bw_limit 200000 2>/dev/null
    hccn_tool -i $c -dscp_to_tc -s dscp 46 tc 0 2>/dev/null
    hccn_tool -i $c -netdetect -s address 0.0.0.0 2>/dev/null
    hccn_tool -i $c -udp -s port 4791 2>/dev/null
  done
fi
rm -f /tmp/dcat-* /tmp/dcat.dstate.* /tmp/dcat.write.* /tmp/dcat.stress.* /tmp/dcat_pwned 2>/dev/null
rm -f /etc/dcat.stress.* /etc/dcat.write.* 2>/dev/null
rm -f /data/dcat.stress.* /data/dcat.write.* /data/dcat-* 2>/dev/null
rm -f "{home}/.demoncat/state.json" 2>/dev/null
# sudo env_reset/always_set_home 会把 HOME 变 /root：旧版/残留 dcat state 可能在 /root，
# 一并删掉避免重注入返回 exit 5（resource already injected）误判 FAIL
rm -f /root/.demoncat/state.json 2>/dev/null
dmsetup ls --target error 2>/dev/null | awk '/^dcat-/{print $1}' | xargs -r -n1 dmsetup remove -f 2>/dev/null
dmsetup ls --target delay 2>/dev/null | awk '/^dcat-/{print $1}' | xargs -r -n1 dmsetup remove -f 2>/dev/null
# losetup: 只清理 dcat 命名空间的 loop（-D 全清会动到 snap/squashfs 等宿主挂载）, 对
# rDISK 类没有难清理的 loop 挂载, 故仅按 dcat- 前缀精确 detach
for lp in $(losetup -a 2>/dev/null | awk -F: '{print $1}' | grep -E '/dcat-'); do
  losetup -d "$lp" 2>/dev/null
done
tc qdisc del dev {iface} root 2>/dev/null
ip link set {iface} up 2>/dev/null
for port in 19998 19999; do
  iptables -D INPUT -p tcp --dport $port -j DROP 2>/dev/null
  iptables -D OUTPUT -p tcp --sport $port -j DROP 2>/dev/null
done
# rNET_tcp_loss: sidecar 记录真实端口（默认测试用 8080），逐条 -D 清, 防残留 DROP 规则
for rule in /tmp/dcat-rNET_tcp_loss-*.rule; do
  [ -f "$rule" ] || continue
  p=${rule##*/dcat-rNET_tcp_loss-}; p=${p%.rule}
  [ "$p" = "19998" ] || [ "$p" = "19999" ] && continue
  case "$p" in *[!0-9]*) continue ;; esac
  iptables -D INPUT -p tcp --dport "$p" -j DROP 2>/dev/null
  iptables -D OUTPUT -p tcp --sport "$p" -j DROP 2>/dev/null
  rm -f "$rule"
done
for f in /tmp/dcat-rCPU_core_offline-c*; do
  [ -f "$f" ] || continue
  n=${f##*/dcat-rCPU_core_offline-c}
  echo 1 > /sys/devices/system/cpu/cpu$n/online 2>/dev/null
done
true
'''


def sweep(home, iface, tracked_pids):
    for p in tracked_pids:
        try:
            os.kill(p, 9)
        except OSError:
            pass
    # 写到临时脚本文件再 sh 执行：避免 pkill -f 'PATTERN' 匹配到执行 sweep 的
    # sh -c "...PATTERN..." 自身 cmdline（会自杀，导致 rm state.json 等后续命令不执行）。
    script = SWEEP_SCRIPT.replace("{home}", home).replace("{iface}", iface)
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".sh", prefix="dcat_e2e_sweep_")
    try:
        os.write(fd, script.encode())
        os.close(fd)
        is_nonroot = hasattr(os, "geteuid") and os.geteuid() != 0
        auto_sudo = is_nonroot and os.environ.get("DCAT_AUTO_SUDO") == "1"
        sh(f"{'sudo -n -E ' if auto_sudo else ''}sh {shlex.quote(path)}", timeout=30)
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


# ---------------- provision（不再 skip：资源就绪则用，不就绪则用例自然 FAIL） ----------------
def provision(provs, ctx, iface):
    """provs: set of provision names. 绑定占位符到 ctx。资源获取失败留空 → 用例 FAIL（生产全量跑）。"""
    if "sleep_pid" in provs:
        # 经 watcher(bash) 派生 target sleep：target.ppid = watcher，不是本框架。
        # 否则 rPROC_zstate clean（kill 父进程回收僵尸）会 kill 本框架自杀。
        p = subprocess.Popen(["bash", "-c", "sleep 600 & echo $!; exec sleep 600"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            line = p.stdout.readline() or b""
            ctx["pid"] = line.decode(errors="replace").strip()
            ctx["_watcher_pid"] = p.pid
        except Exception:
            ctx["pid"] = ""
            ctx["_watcher_pid"] = p.pid
    if "free_port" in provs:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.bind(("", 0))
            port = s.getsockname()[1]
            s.close()
            ctx["port"] = str(port)
        except Exception:
            ctx["port"] = ""
    if "dummy_iface" in provs:
        if os.geteuid() == 0:
            sh(f"ip link add {iface} type dummy 2>/dev/null")
            sh(f"ip link set {iface} up 2>/dev/null")
        ctx.setdefault("iface", iface)  # 不覆盖 real_phy 预置的 iface
    if "real_phy" in provs:
        # 找一个有 speed 的非 dummy 物理网卡（生产通常有；dummy/WSL 无 → 留空，用例 FAIL）
        ok, out = sh("for i in $(ls /sys/class/net 2>/dev/null); do "
                     "case $i in lo|dummy*|veth*|br*|docker*|" + iface + ") continue;; esac; "
                     "s=$(ethtool $i 2>/dev/null | grep -oE 'Speed: [0-9]+'); "
                     "[ -n \"$s\" ] && echo $i && exit 0; done; exit 1")
        if ok == 0 and out.strip():
            ctx["iface"] = out.strip().splitlines()[0]
    if "noncritical_svc" in provs:
        # 仅选可干净 stop/start 的简单服务（cron/chronyd/ntpd）；无则留空 → 用例 FAIL
        svc = ""
        for s in ("cron", "chronyd", "ntpd"):
            if sh(f"systemctl is-active {s} >/dev/null 2>&1")[0] == 0:
                svc = s
                break
        ctx["svc"] = svc


def substitute(s, ctx):
    if not s:
        return s
    for k in ("pid", "port", "iface", "svc", "chip", "dev"):
        s = s.replace("{" + k + "}", ctx.get(k, ""))
    phy = ctx.get("phy_iface", "")
    if phy:
        s = s.replace("--iface=eth0", f"--iface={phy}")
    # rNET_down/rNET_link_flap: eth1 → dummy 接口（不能 down 物理网卡）
    dummy = ctx.get("iface", "")
    if ctx.get("down_safe") and dummy:
        s = s.replace("--iface=eth1", f"--iface={dummy}")
        # eth0 不替换——留给安全防护测试，脚本应拒绝 down 管理网卡
    return s


# ---------------- 断言 DSL ----------------
def apply_assert(vassert, verify_out, cmd_rc, cmd_json):
    """返回 (ok, detail)."""
    if not vassert:
        return True, "(no verify)"
    if vassert.startswith("state_empty"):
        arr = json_state_data(cmd_json)
        return (len(arr) == 0, f"data[] len={len(arr)}")
    if vassert.startswith("state_contains:"):
        uid = vassert.split(":", 1)[1]
        arr = json_state_data(cmd_json)
        return (any(r.get("uid") == uid for r in arr), f"data contains {uid}? {any(r.get('uid')==uid for r in arr)}")
    if vassert.startswith("state_not_contains:"):
        uid = vassert.split(":", 1)[1]
        arr = json_state_data(cmd_json)
        hit = any(r.get("uid") == uid for r in arr)
        return (not hit, f"data not_contains {uid}? {not hit}")
    if vassert.startswith("exitcode:"):
        n = int(vassert.split(":", 1)[1])
        return (cmd_rc == n, f"exitcode {cmd_rc}=={n}")
    if vassert.startswith("exists:"):
        path = vassert.split(":", 1)[1]
        ex = os.path.exists(path)
        return (ex, f"exists {path}: {ex}")
    if vassert.startswith("notexists:"):
        path = vassert.split(":", 1)[1]
        ex = os.path.exists(path)
        return (not ex, f"notexists {path}: {not ex}")
    if not (verify_out or "").strip() and vassert != "empty" \
            and not vassert.startswith(("state_", "exitcode:", "exists:", "notexists:", "out_contains:")):
        # 空输出 = 无证据：负向断言(notcontains/ne/!= 等)不得真空 PASS。
        # 例外：显式 empty 断言；state_*/exitcode:/exists:/notexists:/out_contains: 不依赖 verify_out。
        return (False, f"empty verify output (assert {vassert})")
    # 数值/字符串算子作用于 verify_out
    val = (verify_out or "").strip().splitlines()[-1] if (verify_out or "").strip() else ""
    if vassert.startswith(">="):
        try:
            return (int(val) >= int(vassert[2:]), f"{val} >= {vassert[2:]}")
        except Exception:
            return (False, f"parse fail: '{val}' {vassert}")
    if vassert.startswith("<="):
        try:
            return (int(val) <= int(vassert[2:]), f"{val} <= {vassert[2:]}")
        except Exception:
            return (False, f"parse fail: '{val}' {vassert}")
    if vassert.startswith("=="):
        try:
            return (int(val) == int(vassert[2:]), f"{val} == {vassert[2:]}")
        except Exception:
            return (val == vassert[2:], f"'{val}' == '{vassert[2:]}'")
    if vassert.startswith("!="):
        try:
            return (int(val) != int(vassert[2:]), f"{val} != {vassert[2:]}")
        except Exception:
            return (val != vassert[2:], f"'{val}' != '{vassert[2:]}'")
    if vassert.startswith("eq:"):
        return (val == vassert[3:], f"'{val}' eq '{vassert[3:]}'")
    if vassert.startswith("ne:"):
        return (val != vassert[3:], f"'{val}' ne '{vassert[3:]}'")
    if vassert.startswith("contains:"):
        s = vassert[len("contains:"):]
        return (s in (verify_out or ""), f"contains '{s}'")
    if vassert.startswith("out_contains:"):
        # 作用于命令自身 stdout（cmd_json），用于 query <uid> 的 confirmed 等
        s = vassert[len("out_contains:"):]
        return (s in (cmd_json or ""), f"out contains '{s}'")
    if vassert.startswith("notcontains:"):
        s = vassert[len("notcontains:"):]
        return (s not in (verify_out or ""), f"notcontains '{s}'")
    if vassert.startswith("regex:"):
        pat = vassert[len("regex:"):]
        m = re.search(pat, verify_out or "")
        return (m is not None, f"regex /{pat}/")
    if vassert == "empty":
        return ((verify_out or "").strip() == "", "empty")
    if vassert == "nonempty":
        return ((verify_out or "").strip() != "", "nonempty")
    return (False, f"unknown assert: {vassert}")


def json_state_data(jstr):
    try:
        d = json.loads(jstr)
        data = d.get("data", [])
        if isinstance(data, list):
            return data
        return []
    except Exception:
        return []


def check_precondition(precond):
    """物理前置检查。返回非空字符串 = 跳过原因（SKIP）；返回空串 = 通过。
    仅 RoCE 链路物理 DOWN 等无法靠代码满足的前置才 SKIP；
    hccn_tool 缺失等软件前置不跳过（用例自然 FAIL，保持生产全量跑哲学）。"""
    if not precond or precond == "none":
        return ""
    if precond == "roce_link_up":
        # rNPU_link_down 专用：inject 需链路可拉低、clean 需 -cfg recovery 恢复 UP；
        # 物理无网线/对端 → -s up 返回成功但状态仍 DOWN → clean 无法恢复 → SKIP（非代码问题）
        sh("hccn_tool -i 2 -link -s up 2>/dev/null")
        rc, out = sh("hccn_tool -i 2 -link -g 2>/dev/null")
        if rc != 0 or "up" not in (out or "").lower():
            return "RoCE 链路物理 DOWN（无网线/对端），RoCE 配置类用例无法生效"
    if "sysfs_writable" in precond or ("sysfs" in precond and ("写" in precond or "writable" in precond.lower())):
        # rCPU_core_offline 专用：需 sysfs 可写 + CPU hotplug 支持
        rc, out = sh("cat /sys/devices/system/cpu/cpu1/online 2>/dev/null")
        if rc != 0:
            return "sysfs 不可读或 cpu1 不存在（VM/WSL2 不支持 cpu offline）"
        sudo_pfx = "sudo -n -E " if os.environ.get("DCAT_AUTO_SUDO") == "1" else ""
        # 先检查 cpu1（存在性+可写）
        rc, out = sh("cat /sys/devices/system/cpu/cpu1/online 2>/dev/null")
        if rc != 0:
            return "sysfs 不可读或 cpu1 不存在（VM/WSL2 不支持 cpu offline）"
        # 再试 cpu0（boot CPU，很多 VM 不允许 offline cpu0）
        rc0, out0 = sh(f"{sudo_pfx}sh -c 'echo 0 > /sys/devices/system/cpu/cpu0/online' 2>/dev/null && echo ok || echo fail")
        sh(f"{sudo_pfx}sh -c 'echo 1 > /sys/devices/system/cpu/cpu0/online' 2>/dev/null")
        if "fail" in (out0 or "").lower():
            return "CPU0 不可 offline（VM boot CPU 限制）"
    if "tc_qdisc" in precond or "sch_tbf" in precond:
        # rNET_bw_limit 专用：需 tc 命令和 sch_tbf 模块
        rc, out = sh("command -v tc >/dev/null 2>&1 && echo ok || echo missing")
        if "missing" in out.lower():
            return "tc 命令不可用（iproute2 未安装）"
        rc, out = sh("modprobe sch_tbf 2>/dev/null; lsmod | grep -q sch_tbf && echo ok || echo missing")
        if "missing" in out.lower():
            return "sch_tbf 模块不可用（内核不支持）"
    if "iptables" in precond.lower() or "tcp_loss" in precond.lower():
        # rNET_tcp_loss 专用：需 iptables 可用
        rc, out = sh("command -v iptables >/dev/null 2>&1 && echo ok || echo missing")
        if "missing" in out.lower():
            return "iptables 不可用"
        # 验证 iptables 真的可操作（需要 root 或 sudo）
        sudo_pfx = "sudo -n -E " if os.environ.get("DCAT_AUTO_SUDO") == "1" else ""
        rc, out = sh(f"{sudo_pfx}iptables -C INPUT -p tcp --dport 1 -j DROP 2>/dev/null && echo ok || {sudo_pfx}iptables -A INPUT -p tcp --dport 1 -j DROP 2>/dev/null && {sudo_pfx}iptables -D INPUT -p tcp --dport 1 -j DROP 2>/dev/null && echo ok || echo fail")
        if "fail" in (out or "").lower():
            return "iptables 规则操作失败（权限或内核模块问题）"
    if "SSH" in precond and "管理" in precond:
        # rNET_down 安全防护用例：验证对"管理/SSH 网卡"注入应被拒绝。
        # 该用例假设 eth0 在测试环境不存在（脚本对不存在网卡注入失败 exit 1）。
        # 若环境中真实存在 eth0（如 runner 主网卡），注入会真的 down 管理网卡：
        #   - 会断掉 runner 与 GitHub 的网络（之前 CI 反复断连/重试即此因）
        #   - 注入实际成功 exit 0，与预期 exit 1 冲突
        # => 环境存在 eth0 时 SKIP（安全校验场景不成立，且不可真实演练）。
        rc, out = sh("ip -o link show eth0 2>/dev/null && echo exists || echo missing")
        if "exists" in (out or "").lower():
            return "测试环境存在 eth0 网卡；注入会 down 管理网卡断 runner 网络，安全防护用例需在无 eth0 环境验证"
    if "npu_hardware" in precond or "npu_compute" in precond:
        # rNPU_* 通用基础层：需 Atlas NPU 硬件 + hccn_tool
        rc, out = sh("command -v hccn_tool >/dev/null 2>&1 && echo ok || echo missing")
        if "missing" in out.lower():
            return "hccn_tool 不可用（非 Atlas NPU 机器）"
        # 检测 /dev/davinci* 设备文件（NPU 硬件存在的标志）
        rc, out = sh("ls /dev/davinci* 2>/dev/null | head -1")
        if not out.strip():
            return "NPU 设备不可用（无 /dev/davinci* 设备文件）"
        # RoCE 层：网络类模块需 NPU RoCE 口（hccn_tool -status -g 报告，不在 host ip link 里）
        if "npu_hardware" in precond and "npu_compute" not in precond:
            _first = out.strip().splitlines()[-1] if out.strip() else "0"
            _first = _first.replace("/dev/davinci", "")
            rc, out = sh(f"hccn_tool -i {_first} -status -g 2>/dev/null | grep -q 'Settings for' && echo ok || echo missing")
            if "missing" in out.lower():
                return "NPU RoCE 口不可用（hccn_tool -status -g 无报告）"
    if "mock" in precond.lower() and "可用" in precond:
        return "mock 环境不可用（需要 mock dcat 二进制）"
    if "serve" in precond and "长超时" in precond:
        # serve 默认为长时阻塞进程：run_step_cmd 120s 超时会得到 rc=124，
        # TC-555(exitcode:124) 正是利用该超时语义验证 serve 阻塞行为。不再无条件 SKIP。
        return ""
    if "非 tmpfs" in precond:
        rc, out = sh("df -t tmpfs /tmp 2>/dev/null | grep -q tmpfs && echo yes || echo no")
        if "yes" in out.lower():
            return "/tmp 是 tmpfs（dd 写入导致 OOM，需真实磁盘目录如 /data）"
    if "service_stop" in precond.lower() or ("service" in precond.lower() and "stop" in precond.lower()):
        # rNET_service_stop 专用：需目标 service 存在。
        # 服务名优先取前置文本中的 --service=X；无则回退尝试常见候选
        # （cron/chronyd/ntpd，对应 provisionnoncritical_svc），最后回退 nginx。
        import re as _re2
        m2 = _re2.search(r'--service=(\S+)', precond)
        svc = m2.group(1) if m2 else ""
        # 前置文本可能形如 "cron 服务存在" / "已注入 rNET_service_stop"，无 service 名
        for candidate in (svc, "cron", "chronyd", "ntpd", "nginx"):
            if not candidate:
                continue
            rc, out = sh(f"command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files 2>/dev/null | grep -q '^{candidate}\\.' && echo ok || echo missing")
            if "ok" in out.lower():
                return ""
        return "无可用目标 service（cron/chronyd/ntpd/nginx 均未安装）"
    if "non-root" in precond:
        if hasattr(os, "geteuid") and os.geteuid() == 0:
            return "当前为 root，非 root 权限测试需降权运行"
        if os.environ.get("DCAT_AUTO_SUDO") == "1":
            return "DCAT_AUTO_SUDO=1（CI 非 root + sudo），非 root 拒绝测试无法验证"
    if "配置文件" in precond and "存在" in precond:
        import re as _re
        m = _re.search(r'(/[\S]+\.conf)', precond)
        if m:
            path = m.group(1)
            rc, _ = sh(f"test -f {path}")
            if rc != 0:
                return f"配置文件 {path} 不存在"
    return ""
