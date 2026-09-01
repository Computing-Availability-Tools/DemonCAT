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


def _path_from_vcmd(vcmd):
    """从 verify 观测命令中提取目标路径（用于裸 notexists 断言）。

    仅支持 `ls <path>` 形态（可带尾部 2>&1/2>/dev/null 重定向）。
    裸 notexists 语义 = '该路径文件不存在'。无法提取 → 返回 None。
    """
    v = (vcmd or "").strip()
    # 去掉尾部 shell 重定向（2>&1 / 2>/dev/null）
    v = re.sub(r'\s*2>\S+.*$', '', v).strip()
    for kw in ("ls ", "cat "):
        if v.startswith(kw) and " " in v:
            path = v[len(kw):].strip()
            # 禁止含通配符、管道、内嵌空格（分离路径与命令）的复合形态 → 无法判路径
            if path and "*" not in path and "?" not in path and "|" not in path and " " not in path:
                return path
    return None

SKIP_MODULES = {
    "rNPU_link_down",
}


def _is_runnable_vcmd(vcmd):
    return bool(vcmd) and any(k in vcmd for k in _RUNKW) and not any(m in vcmd for m in _NA_MARKERS)


# 改值型 NPU 故障的生命周期前置：注入目标必须产生真实变更（目标值≠中途机器当前值），
# 否则 hccn 回读校验 no-op → "注入回读校验失败:动作未生效"。前置原则=注入值≠当前值；
# 若相等（机器漂移/崩溃残留/基线异常）→ SKIP 并明确提示，而非注入阶段失败。
# 条目增删型（arp/route/iproute/iprule）无此约束（add/del 幂等）。
_NPU_PARAM_READ = {
    "rNPU_gw_change":         ("-gateway -g",  "gateway", r'([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)'),
    "rNPU_ip_change":         ("-ip -g",       "address", r'ipaddr:([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)'),
    "rNPU_netdetect_change":  ("-netdetect -g", "address", r'address:\s*([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)'),
    "rNPU_mtu_mismatch":      ("-mtu -g",      "size",    r'mtu:([0-9]+)'),
    "rNPU_bw_limit":          ("-shaping -g",  "bw_limit", r'bw_limit\[([0-9]+)'),
    "rNPU_roce_port_change":  ("-udp -g",      "port",    r'udp_port:([0-9]+)'),
}


def _npu_target_collisions(ctx, all_injects):
    """返回与当前机器值冲突的 (uid, target, current) 列表；无冲突返回 []。

    all_injects: 注入命令 argv 列表（setup 前置注入 + 步骤注入均检查）。
    """
    chip = ctx.get("chip", "")
    if not chip:
        return []
    try:
        import shutil
        if not shutil.which("hccn_tool"):
            return []
    except Exception:
        return []
    hits = []
    seen = set()
    for argv in all_injects:
        # setup_argv 可为 None（无前置注入）——跳过，绝不能让 len(None) 炸掉整个守卫
        if not argv:
            continue
        if len(argv) < 4 or argv[0] != "dcat" or argv[1] != "inject":
            continue
        uid = argv[2]
        if uid not in _NPU_PARAM_READ:
            continue
        readopt, key, pat = _NPU_PARAM_READ[uid]
        target = None
        for a in argv[3:]:
            if a.startswith("--") and "=" in a:
                k, v = a[2:].split("=", 1)
                if k == key:
                    target = v
        if target is None:
            continue
        sig = (uid, target)
        if sig in seen:
            continue
        seen.add(sig)
        rc, out = sh(f"timeout 8 hccn_tool -i {chip} {readopt} 2>/dev/null", timeout=15)
        m = re.search(pat, out or "")
        cur = m.group(1) if m else ""
        if cur and cur == target:
            hits.append((uid, target, cur))
    return hits


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
    # 裸 notexists：转成可判定断言（否则被 e2e_assert._is_skip_value 判为 skip →
    # "clean 后工件删除"类断言全部空转）。
    #   - ls <path> 纯路径 → notexists:<path>（按文件系统判）
    #   - 含通配符/管道无法定位路径 → notexists_rc:（按 verify 输出判是否"文件不存在"）
    if (vassert or "").strip() == "notexists":
        vctx = substitute(case.vcmd, ctx)
        p = _path_from_vcmd(vctx)
        vassert = f"notexists:{p}" if p else "notexists_rc:"
    if vassert and vassert.startswith("state_"):
        # state_* 断言对比的 uid 取断言目标（clean 后 "rNPU_arp 无幽灵记录" 查询
        # 的就是 rNPU_arp，而非标题拆出的伪 uid 后缀 rNPU_arp_del）。标题模块用于
        # marker/sweep 归类；状态查询必须用真实 fault uid，否则恒空 → 假阳性。
        q_uid = case.module
        if ":" in vassert:
            _su = vassert.split(":", 1)[1].strip()
            if _su and _su != "<uid>":
                q_uid = _su
        if verb == "query":
            state_data = state_data_of(cmd_out)
            confirmed = confirmed_of(cmd_out)
        else:
            state_data, _rc, qout = query_state(q_uid, env=env, dcat_bin=dcat)
            confirmed = confirmed_of(qout)
            recorder.verify_cmd = "(state query)"
            recorder.verify_out = qout
    else:
        if _is_runnable_vcmd(case.vcmd):
            time.sleep(0.6)
            vcmd = substitute(case.vcmd, ctx)
            # cmds 里 --pid=12345 占位在 test_case 中已被替换为真实 pid；verify 的 vcmd
            # 同样要替换——否则 `dcat query rPROC_hang --pid=12345` 查不存在的 pid → 恒假
            # FAIL（TC-622）。substitute 只处理 {pid}，需补 --pid=<n> 字面量替换。
            if ctx.get("pid"):
                vcmd = re.sub(r'--pid=\d+', f"--pid={ctx['pid']}", vcmd)
            # verify 的 dcat 命令也要走 run_step_cmd（argv 防注入 + auto_sudo 时
            # 注入 env HOME=E2E_HOME）。否则 sudo env_reset/always_set_home 把 HOME
            # 重置为 /root 时，verify 的 dcat query 会读 /root/.demoncat/state.json，
            # 与 run_step_cmd 注入（E2E_HOME）写的 state 对不上 → 系统性假 FAIL（
            # 与 d2503b0 修的 exit-5 残留 bug 同根因）。
            if vcmd.startswith("dcat "):
                # 先换成绝对二进制路径：run_step_cmd 的 argv 分支只认绝对路径前缀，
                # 裸 'dcat' 会落入 shell 执行并依赖 PATH（本机/CI 均不在 PATH → command
                # not found → ~99 条 confirmed:true 断言全 FAIL、confirmed:true→false
                # 假 PASS）。换绝对路径后仍走 argv + HOME 注入。
                vcmd_abs = f"{dcat} {vcmd[5:]}"
                _rc, so, se = run_step_cmd(vcmd_abs, env=env, dcat_bin=dcat, timeout=30)
                verify_out = (so or "") + (se or "")
            else:
                # 非 dcat verify 命令（iptables -L / cat /sys 等）：auto_sudo 加 sudo
                _auto_sudo = hasattr(os, "geteuid") and os.geteuid() != 0 and os.environ.get("DCAT_AUTO_SUDO") == "1"
                if _auto_sudo and not vcmd.startswith("sudo"):
                    vcmd = f"sudo -n -E {vcmd}"
                verify_out = sh(vcmd, env=env, timeout=30)[1]
            recorder.verify_cmd = case.vcmd
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
    # 管理网卡安全防护(TC-105)：改用结构判定——只有 inject/clean 目标是 eth0
    # (管理网卡)的用例才受 eth0 存在性 SKIP 保护；down_safe 用例(eth1→dummy)即使
    # 措辞含"SSH/管理网卡"也不触发，避免 TC-109/137/145 误 SKIP。
    targets_eth0 = any(
        len(a) > 2 and a[0] == "dcat" and a[1] in ("inject", "clean") and "--iface=eth0" in a
        for a in case.cmds)
    if not targets_eth0:
        pre = pre.replace("SSH", "").replace("管理网卡", "")
    if pre in ("none", "roce_link_up"):
        coded_pre = pre
    elif any(kw in pre for kw in ("sysfs_writable", "tc_qdisc", "sch_tbf", "npu_hardware", "npu_compute", "non-root", "配置文件", "mock", "serve", "非 tmpfs", "SSH", "管理网卡", "iptables", "service_stop")):
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
        if has_hw_marker and "npu_hardware" not in (case.precondition or "") and "npu_compute" not in (case.precondition or ""):
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
               "chip": "", "phy_iface": "", "down_safe": True}
    else:
        ctx = {"iface": phy or e2e_env["iface"], "pid": "", "port": "", "svc": "",
               "chip": "", "phy_iface": phy}

    # NPU tests: dynamically detect chip (Phy-ID) and RoCE port name
    if mod_lower.startswith("rnpu") or any(
        len(a) > 2 and a[1] in ("inject", "clean", "query") and a[2].lower().startswith("rnpu")
        for a in case.cmds):
        _rc, _out = sh("ls /dev/davinci* 2>/dev/null | sort -V | head -1 | grep -oE '[0-9]+'")
        _chip = _out.strip().splitlines()[0] if _out.strip() else "0"
        ctx["chip"] = _chip
        # Detect NPU RoCE port name (e.g. eth0) from hccn_tool -status -g
        if "npu_hardware" in (case.precondition or ""):
            _rc, _out = sh(f"hccn_tool -i {_chip} -status -g 2>/dev/null | grep -oE 'Settings for \\w+' | awk '{{print $3}}'")
            _dev = _out.strip().splitlines()[0] if _out.strip() else "eth0"
            ctx["dev"] = _dev

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
    if phy or ctx.get("down_safe") or ctx.get("chip") or ctx.get("dev"):
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
            # setup_argv 来自 steps 首条 inject 时可能已含占位 --pid=12345，
            # 必须替换为真实 pid，否则 inject --pid=12345（不存在）→ setup 失败 → SKIP
            if setup_argv:
                replaced = False
                for i, a in enumerate(setup_argv):
                    if _PID_RE.match(a) or _PID_RE.search(a):
                        setup_argv[i] = f"--pid={ctx['pid']}"
                        replaced = True
                if not any("--pid=" in a for a in setup_argv):
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
    # setup 缺参数时从 cmds 提取 DA: 抄同 uid 的 inject/clean/query 命令参数
    # （xlsx "已注入" setup 常不带参数）。绝不从不同 uid 的命令抄——TC-157 前置注入
    # rNET_loss(占 qdisc)，step 是 rNET_jitter --delay_ms；把 jitter 参数塞给
    # rNET_loss 会报 unknown parameter 'delay_ms'。
    if setup_argv and len(setup_argv) > 2 and setup_argv[1] == "inject":
        setup_uid = setup_argv[2]
        setup_keys = {a.split("=")[0] for a in setup_argv if a.startswith("--")}
        for argv in case.cmds:
            if len(argv) > 2 and argv[0] == "dcat" and argv[1] in ("inject", "clean", "query") and argv[2] == setup_uid:
                for arg in argv[3:]:
                    key = arg.split("=")[0]
                    if arg.startswith("--") and key not in setup_keys and key != "--force":
                        setup_argv.append(arg)
                        setup_keys.add(key)
        # rNPU 默认补 chip/dev + 各模块必填参数（setup 无 inject 命令可抄时）
        if setup_uid.lower().startswith("rnpu"):
            _npu_defaults = {
                "rnpu_arp": ["--dev={dev}", "--ip=10.30.12.200", "--mac=00:11:22:33:44:55"],
                "rnpu_bw_limit": ["--bw_limit=50000"],
                "rnpu_dscp_tc_change": ["--dscp=46", "--tc=3"],
                "rnpu_mtu_mismatch": ["--size=1280"],
                "rnpu_netdetect_change": ["--address=10.0.0.99"],
                "rnpu_roce_port_change": ["--port=4792"],
                "rnpu_gw_change": ["--gateway=10.0.0.1"],
                "rnpu_ip_change": ["--address=10.0.0.50", "--netmask=255.255.255.0"],
                "rnpu_iproute": ["--ip=10.30.50.0", "--ip_mask=24", "--via=10.0.0.254", "--dev={dev}", "--table=100"],
                "rnpu_iprule": ["--dir=from", "--ip=10.30.12.210", "--table=150"],
                "rnpu_route": ["--address=10.30.40.0", "--netmask=255.255.255.0", "--gateway=10.0.0.254"],
            }
            _uid_l = setup_uid.lower()
            if "--chip" not in setup_keys:
                setup_argv.append("--chip={chip}")
                setup_keys.add("--chip")
            for d in _npu_defaults.get(_uid_l, []):
                key = d.split("=")[0]
                if key not in setup_keys:
                    setup_argv.append(d)
                    setup_keys.add(key)
    # 最后做 substitute（eth0→ksdev0, {chip}→动态探测 等）
    if setup_argv:
        setup_argv = [substitute(a, ctx) for a in setup_argv]

    # 3b. 改值型 NPU 前置守卫：注入目标==当前机器值 → 该用例此时无法产生真实变更
    # （下轮会在注入阶段报"注入回读校验失败"），SKIP 并提示（机器漂移时先 dcat clean / 复位基线）。
    try:
        coll = _npu_target_collisions(ctx, [setup_argv] + cmds)
    except Exception:
        coll = []
    if coll:
        uid, target, cur = coll[0]
        msg = f"{case.id}: 注入值 {uid} --{_NPU_PARAM_READ[uid][1]}={target} == 当前机器值 {cur}，无法产生真实变更（机器漂移？）"
        recorder.detail = msg
        pytest.skip(msg)

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
