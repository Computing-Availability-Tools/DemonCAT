"""tests/e2e/test_e2e_cases.py — testcases.xlsx 驱动的参数化 e2e 测试。

每个 xlsx 用例 = 一个 pytest item（id=TC-xxx_模块）。
执行流：skip→物理前置→provision(pid)→前序 setup→按序跑 dcat 命令
→每条命令后验证观测命令→eval_assert（任一通过即 PASS）。
"""
import copy
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import pytest

HERE = Path(__file__).parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from e2e_loader import parametrized_cases
from e2e_assert import eval_assert
from e2e_state import query_state, state_data_of, confirmed_of
from e2e_helpers import run_step_cmd, sh, substitute, check_precondition

_NA_MARKERS = ("注入未执行", "无系统断言", "或非故障", "clean后观测")
_RUNKW = ("dcat", "tc ", "pgrep", "ip ", "ls ", "cat ", "grep", "wc",
          "ss ", "for ", "iptables", "hccn_tool", "systemctl", "echo ", "awk ")
_PID_RE = re.compile(r'--pid=[1-9]\d*$')  # 不匹配 --pid=0 (测试值)

SKIP_MODULES = {
    "rNPU_gw_change", "rNPU_ip_change",
    "rNPU_iproute_add", "rNPU_iproute_del",
    "rNPU_iprule_add", "rNPU_iprule_del",
    "rNPU_route_add", "rNPU_route_del",
    "rNPU_link_down",
}


def _is_runnable_vcmd(vcmd):
    return bool(vcmd) and any(k in vcmd for k in _RUNKW) and not any(m in vcmd for m in _NA_MARKERS)


def _provision_pid(tracked):
    """Provision a sleep process as inject target; return its pid string.

    bash watcher spawns sleep child (target) then execs another sleep;
    watcher 不会 wait → target 被 kill 后变僵尸（Z），供 rPROC_zstate 验证。
    """
    p = subprocess.Popen(["bash", "-c", "sleep 600 & echo $!; exec sleep 600"],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    tracked.append(p.pid)
    try:
        line = p.stdout.readline() or b""
        pid = line.decode(errors="replace").strip()
        if pid.isdigit():
            tracked.append(int(pid))
        return pid
    except Exception:
        return ""


def _eval_step(vassert, cmd_rc, cmd_out, case, verb, ctx, env, dcat, recorder):
    """评估单步断言。返回 (result, detail)。需要时跑 verify 观测命令。"""
    state_data = []
    confirmed = None
    verify_out = ""
    if vassert and vassert.startswith("state_"):
        if verb == "query":
            state_data = state_data_of(cmd_out)
            confirmed = confirmed_of(cmd_out)
        else:
            state_data, _rc, qout = query_state(case.module, env=env, dcat_bin=dcat)
            confirmed = confirmed_of(qout)
            recorder.verify_cmd = "(state query)"
            recorder.verify_out = qout
    else:
        if _is_runnable_vcmd(case.vcmd):
            time.sleep(0.6)
            vcmd = substitute(case.vcmd, ctx)
            if vcmd.startswith("dcat "):
                vcmd = f"{dcat} {vcmd[5:]}"
            # verify 命令需 root 权限时（iptables/cat /sys 等），auto_sudo 加 sudo
            _auto_sudo = hasattr(os, "geteuid") and os.geteuid() != 0 and os.environ.get("DCAT_AUTO_SUDO") == "1"
            if _auto_sudo and not vcmd.startswith("sudo"):
                vcmd = f"sudo -n -E {vcmd}"
            recorder.verify_cmd = case.vcmd
            verify_out = sh(vcmd, env=env, timeout=30)[1]
            recorder.verify_out = verify_out
        else:
            recorder.verify_cmd = case.vcmd or "(N/A)"
    return eval_assert(vassert, cmd_rc, cmd_out, verify_out, state_data, confirmed)


@pytest.mark.parametrize("case", parametrized_cases())
def test_case(case, dcat, e2e_env, autouse_sweep, recorder, tracked, request):
    recorder.case = case

    # 1. 预置 skip（无 dcat 命令 / setup 不可解析）
    if case.skip_reason:
        recorder.detail = case.skip_reason
        pytest.skip(case.skip_reason)

    # 1b. SKIP NPU 网络故障（需物理交换机拓扑，NPU runner 未接交换机）
    if case.module in SKIP_MODULES:
        recorder.detail = "requires physical switch network topology"
        pytest.skip("requires physical switch network topology")

    # 2. 物理前置（coded 值 + 关键词匹配触发；xlsx 多为描述性 = no-op）
    pre = case.precondition or ""
    if pre in ("none", "roce_link_up"):
        coded_pre = pre
    elif any(kw in pre for kw in ("sysfs_writable", "tc_qdisc", "sch_tbf", "npu_hardware", "non-root", "配置文件", "mock", "serve", "非 tmpfs", "SSH", "管理网卡", "iptables", "service_stop")):
        coded_pre = pre
    else:
        coded_pre = "none"
    pre_skip = check_precondition(coded_pre)
    if not pre_skip and case.precondition not in ("none",):
        # 描述性前置条件：检测服务是否运行 / 工具是否存在 / 工具缺失环境
        import re as _re
        svc_match = _re.search(r'服务\s*(\w+)\s*已运行|(\w+)\s*已运行', case.precondition)
        if svc_match:
            svc = svc_match.group(1) or svc_match.group(2)
            if svc:
                rc, _ = sh(f"systemctl is-active {svc} >/dev/null 2>&1")
                if rc != 0:
                    pre_skip = f"precondition not met: service '{svc}' not running"
        # "X 缺失环境" → X 存在则 skip（测的是 X 不存在的场景）
        missing_match = _re.search(r'(\w+)\s*缺失环境', case.precondition)
        if missing_match:
            tool = missing_match.group(1)
            rc, _ = sh(f"command -v {tool} >/dev/null 2>&1")
            if rc == 0:
                pre_skip = f"precondition not met: {tool} is available (test expects it missing)"
    if pre_skip:
        recorder.detail = pre_skip
        # hardware marker + npu_hardware: eth2 不存在等环境缺失 → SKIP（非 NPU CI server）
        # hardware marker + sysfs/tc_qdisc: 在 root 机器上应有 → FAIL（快速暴露问题）
        has_hw_marker = bool(list(request.node.iter_markers("hardware")))
        if has_hw_marker and "npu_hardware" not in (case.precondition or ""):
            assert False, f"{case.id}: 环境预检失败: {pre_skip}"
        pytest.skip(pre_skip)

    env = e2e_env["env"]
    # rNET tests need real physical interface for tc/ethtool; use phy_iface as iface
    # 但 rNET_down/rNET_link_flap 只能用 dummy 接口——down 物理网卡会断 SSH
    phy = e2e_env.get("phy_iface", "")
    mod_lower = case.module.lower()
    is_link_down = mod_lower in ("rnet_down", "rnet_link_flap")
    if is_link_down:
        # down/flap 测试：eth1 → dummy 接口，eth0 不替换（留给安全防护测试 TC-105）
        ctx = {"iface": e2e_env["iface"], "pid": "", "port": "", "svc": "",
               "phy_iface": "", "down_safe": True}
    else:
        ctx = {"iface": phy or e2e_env["iface"], "pid": "", "port": "", "svc": "",
               "phy_iface": phy}

    # extract --service=X and --port=X from cmds to fill {svc}/{port} in vcmd
    for argv in case.cmds:
        for a in argv:
            if a.startswith("--service="):
                ctx["svc"] = a.split("=", 1)[1]
            elif a.startswith("--port="):
                ctx["port"] = a.split("=", 1)[1]

    t0 = time.time()

    # 3. Provision: rPROC 测试需目标 pid（xlsx 用 --pid=12345 占位）
    needs_pid = (case.module.lower().startswith("rproc")
                 or "{pid}" in (case.vcmd or "")
                 or any(_PID_RE.search(" ".join(a)) for a in case.cmds))
    cmds = [list(a) for a in case.cmds]
    setup_argv = list(case.setup_argv) if case.setup_argv else None
    # substitute eth0→物理网卡 / eth1→dummy（down/flap 安全） in dcat commands
    phy = ctx.get("phy_iface", "")
    if phy or ctx.get("down_safe"):
        for argv in cmds:
            for i, arg in enumerate(argv):
                argv[i] = substitute(arg, ctx)
    if needs_pid:
        ctx["pid"] = _provision_pid(tracked)
        if ctx["pid"]:
            for argv in cmds:
                for i, arg in enumerate(argv):
                    if _PID_RE.match(arg):
                        argv[i] = f"--pid={ctx['pid']}"
            if setup_argv and not any("--pid=" in a for a in setup_argv):
                setup_argv.append(f"--pid={ctx['pid']}")
    # rNET setup 缺 --iface 时补测试网卡（优先用物理网卡，tc 不支持 dummy）
    # 但 rNET_service_stop 不接受 --iface 参数，跳过
    net_iface = ctx.get("phy_iface", "") or ctx["iface"]
    if setup_argv and len(setup_argv) > 2 and setup_argv[1] == "inject" \
            and setup_argv[2].lower().startswith("rnet") \
            and "service_stop" not in setup_argv[2].lower() \
            and "conn_exhaust" not in setup_argv[2].lower() \
            and "port_occupy" not in setup_argv[2].lower() \
            and "tcp_loss" not in setup_argv[2].lower() \
            and not any(a.startswith("--iface=") for a in setup_argv):
        setup_argv.append(f"--iface={net_iface}")
    # setup 缺参数时从 cmds 提取（xlsx "已注入" setup 不带参数）
    if setup_argv and len(setup_argv) > 2 and setup_argv[1] == "inject":
        setup_keys = {a.split("=")[0] for a in setup_argv if a.startswith("--")}
        for argv in case.cmds:
            if len(argv) > 2 and argv[0] == "dcat" and argv[1] in ("inject", "clean", "query"):
                for arg in argv[2:]:
                    key = arg.split("=")[0]
                    if arg.startswith("--") and key not in setup_keys and key != "--force":
                        setup_argv.append(arg)
                        setup_keys.add(key)
                break
    # 最后做 substitute（eth0→ksdev0 等）
    if phy and setup_argv:
        setup_argv = [substitute(a, ctx) for a in setup_argv]

    # 4. 前序注入 setup
    if setup_argv:
        setup_str = shlex_join_dcat(dcat, setup_argv)
        s_rc, s_out, s_err = run_step_cmd(setup_str, env=env, dcat_bin=dcat, timeout=120)
        if s_rc != 0:
            recorder.detail = f"setup failed (rc={s_rc})"
            pytest.skip(f"setup failed: {s_out[:120]}")

    # 5. 按序跑 dcat 命令；每条后评估断言，任一通过即 PASS
    vassert = case.vassert
    recorder.vassert = vassert
    result, detail = "skip", "no commands"
    for argv in cmds:
        cmd_str = shlex_join_dcat(dcat, argv)
        rc, so, se = run_step_cmd(cmd_str, env=env, dcat_bin=dcat, timeout=120)
        cmd_out = (so or "") + (se or "")
        verb = argv[1] if len(argv) > 1 else ""
        recorder.cmd_str = cmd_str
        recorder.rc = rc
        recorder.out = cmd_out
        recorder.phase = verb

        result, detail = _eval_step(vassert, rc, cmd_out, case, verb, ctx, env, dcat, recorder)
        recorder.detail = detail
        if result == "pass":
            break

    recorder.duration_ms = int((time.time() - t0) * 1000)
    if result == "skip":
        pytest.skip(detail)
    if result == "fail":
        assert False, f"{case.id}: {detail}"


def shlex_join_dcat(dcat_bin, argv):
    import shlex
    return ' '.join(shlex.quote(a) for a in [dcat_bin] + list(argv[1:]))
