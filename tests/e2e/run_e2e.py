#!/usr/bin/env python3
"""tests/e2e/run_e2e.py — dcat e2e 测试框架（串行 + 每例前后环境恢复）。

读 tests/e2e/cases.csv → 预检(不满足 skip) → provision → 跑 dcat 命令 →
linux 命令观测断言 → 前后幂等清扫 → 输出 results_<ts>.csv + report.md，
并把汇总 append 到 test_report.md §10。

用法:
  python3 tests/e2e/run_e2e.py                      # 跑全部非root + skip root/NPU
  sudo python3 tests/e2e/run_e2e.py                  # 跑含 root
  python3 tests/e2e/run_e2e.py --flows F-rCPU_overload,I-1   # 只跑指定 flow
  python3 tests/e2e/run_e2e.py --no-append           # 不回填 test_report.md
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
            p = subprocess.run(argv, capture_output=True, text=True, env=env,
                               timeout=timeout, cwd=ROOT)
            return p.returncode, (p.stdout or ""), (p.stderr or "")
        except subprocess.TimeoutExpired:
            return 124, "", "[timeout]"
        except Exception as e:
            return 1, "", f"[exception {e}]"
    return sh_sep(cs, env=env, timeout=timeout)


def cmd_exists(c):
    return sh(f"command -v {c} >/dev/null 2>&1")[0] == 0


# ---------------- 环境清扫（dcat 命名空间内，对宿主安全） ----------------
SWEEP_SCRIPT = r'''
set +e
pkill -f 'perl -e' 2>/dev/null
pkill -x yes 2>/dev/null
pkill -9 -f 'dd if=/dev/zero of=.*dcat' 2>/dev/null
sleep 0.5
pkill -9 -f 'dd if=/dev/zero of=.*dcat' 2>/dev/null
pkill -f 'dcat-rNET_port_occupy' 2>/dev/null
pkill -f 'ip link set.*down' 2>/dev/null
pkill -f 'ip link set.*up' 2>/dev/null
rm -f /tmp/dcat-* /tmp/dcat.dstate.* /tmp/dcat.write.* /tmp/dcat.stress.* /tmp/dcat_pwned 2>/dev/null
rm -f /etc/dcat.stress.* /etc/dcat.write.* 2>/dev/null
rm -f "{home}/.demoncat/state.json" 2>/dev/null
tc qdisc del dev {iface} root 2>/dev/null
ip link set {iface} up 2>/dev/null
for port in 19998 19999; do
  iptables -D INPUT -p tcp --dport $port -j DROP 2>/dev/null
  iptables -D OUTPUT -p tcp --sport $port -j DROP 2>/dev/null
done
for f in /tmp/dcat-rCPU_core_offline-c*; do
  [ -f "$f" ] || continue
  n=${f##*/dcat-rCPU_core_offline-c}
  echo 1 > /sys/devices/system/cpu/cpu$n/online 2>/dev/null
done
# NPU stale state cleanup (only when NPU artifacts exist, to avoid 4s+ per sweep)
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
        sh(f"sh {shlex.quote(path)}", timeout=30)
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
    if "loop_device" in provs:
        if os.geteuid() == 0:
            sh("dmsetup remove dcat-error-* 2>/dev/null; dmsetup remove dcat-delay-* 2>/dev/null; true")
            sh("truncate -s 64M /tmp/dcat-e2e-loop.img 2>/dev/null")
            ok, out = sh("losetup -f --show /tmp/dcat-e2e-loop.img 2>/dev/null")
            ctx["loop_dev"] = out.strip() if (ok == 0 and out.strip()) else ""
        else:
            ctx["loop_dev"] = ""
    if "docker_container" in provs:
        ctr = "dcat-e2e-docker"
        sh(f"docker rm -f {ctr} 2>/dev/null")
        # 用本地已有镜像（cann 带 python3.11），避免依赖网络拉取
        ok = sh(f"docker run -d --entrypoint sleep --name {ctr} "
                f"swr.cn-south-1.myhuaweicloud.com/ascendhub/cann:"
                f"8.5.0.alpha001-910b-openeuler24.03-py3.11 600 2>/dev/null")[0]
        ctx["ctr"] = ctr if ok == 0 else ""


def substitute(s, ctx):
    if not s:
        return s
    for k in ("pid", "port", "iface", "svc", "loop_dev", "ctr",
              "e2e_chip", "e2e_npu_id"):
        s = s.replace("{" + k + "}", ctx.get(k, ""))
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
    return ""


# ---------------- 主流程 ----------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", default=CASES)
    ap.add_argument("--dcat", default=DCAT)
    ap.add_argument("--out-dir", default=HERE)
    ap.add_argument("--report", default=os.path.join(ROOT, "test_report.md"))
    ap.add_argument("--no-append", action="store_true")
    ap.add_argument("--flows", default="", help="逗号分隔 flow_id 前缀过滤")
    ap.add_argument("--exclude", default="",
                    help="逗号分隔 flow_id 前缀排除（双轨：ubuntu 子集排除 NPU/硬件依赖 flow）")
    args = ap.parse_args()

    if not os.path.exists(args.dcat):
        print(f"ERROR: dcat 不存在: {args.dcat} (先 cmake --build build)", file=sys.stderr)
        return 1

    rows = list(csv.DictReader(open(args.cases)))
    flows = {}
    for r in rows:
        flows.setdefault(r["flow_id"], []).append(r)
    for fid in flows:
        flows[fid].sort(key=lambda x: int(x["step"]))

    filt = [p for p in args.flows.split(",") if p] if args.flows else None
    excl = [p for p in args.exclude.split(",") if p] if args.exclude else None

    os.makedirs(E2E_HOME, exist_ok=True)
    env = dict(os.environ)
    env["HOME"] = E2E_HOME
    env["E2E_HOME"] = E2E_HOME

    # 框架启动：root 创建测试网卡
    if os.geteuid() == 0:
        sh(f"ip link add {TEST_IFACE} type dummy 2>/dev/null")
        sh(f"ip link set {TEST_IFACE} up 2>/dev/null")
        ipt = True
    else:
        ipt = False

    results = []
    counters = {"PASS": 0, "FAIL": 0, "SKIP": 0}
    cat_stats = {}
    findings = []
    tracked_pids = []
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    fail_log_path = os.path.join(args.out_dir, f"failures_{ts}.log")

    def cleanup_all():
        try:
            print("[phase] atexit cleanup start", flush=True)
            sweep(E2E_HOME, TEST_IFACE, tracked_pids)
            if os.geteuid() == 0:
                sh(f"ip link del {TEST_IFACE} 2>/dev/null", timeout=15)
            # 无条件清理 NPU 测试残留（route/ip_route/ip_rule 由 *_del 的 clean 恢复导致）
            if shutil.which("hccn_tool") or os.path.exists("/usr/bin/hccn_tool") or os.path.exists("/usr/local/Ascend/driver/tools/hccn_tool"):
                for c in ("2", "5"):
                    sh(f"hccn_tool -i {c} -route -d address 10.30.40.0 netmask 255.255.255.0 2>/dev/null")
                    sh(f"hccn_tool -i {c} -route -d address 10.30.41.0 netmask 255.255.255.0 2>/dev/null")
                    sh(f"hccn_tool -i {c} -ip_route -d ip 10.30.50.0 ip_mask 24 table 100 2>/dev/null")
                    sh(f"hccn_tool -i {c} -ip_route -d ip 10.30.51.0 ip_mask 24 table 100 2>/dev/null")
                    sh(f"hccn_tool -i {c} -ip_rule -d dir from ip 10.20.10.210 2>/dev/null")
                    sh(f"hccn_tool -i {c} -ip_rule -d dir from ip 10.20.10.211 2>/dev/null")
                    sh(f"hccn_tool -i {c} -mtu -s size 1500 2>/dev/null")
                    sh(f"hccn_tool -i {c} -shaping -s bw_limit 200000 2>/dev/null")
                    sh(f"hccn_tool -i {c} -dscp_to_tc -s dscp 46 tc 0 2>/dev/null")
                    sh(f"hccn_tool -i {c} -netdetect -s address 0.0.0.0 2>/dev/null")
                    sh(f"hccn_tool -i {c} -udp -s port 4791 2>/dev/null")
                print("[phase] atexit NPU cleanup done", flush=True)
            print("[phase] atexit cleanup done", flush=True)
        except Exception as e:
            print(f"[phase] atexit cleanup error: {e}", flush=True)

    import atexit
    atexit.register(cleanup_all)

    flow_ids = sorted(flows.keys())
    for fid in flow_ids:
        if filt and not any(fid.startswith(p) or p == fid for p in filt):
            continue
        if excl and any(fid.startswith(p) for p in excl):
            continue
        steps = flows[fid]
        cat = fid.split("-")[0]
        # 原则上不 skip：生产要求全量跑，资源未就绪→用例自然 FAIL；
        # 例外：物理前置缺失（如 RoCE 无网线）→ SKIP（见 check_precondition）。
        ctx = {}
        # NPU chip/npu_id from env (default: chip=2, npu_id=2)
        ctx["e2e_chip"] = os.environ.get("DCAT_E2E_CHIP", "2")
        ctx["e2e_npu_id"] = os.environ.get("DCAT_E2E_NPU_ID", "2")

        # 前置清扫
        sweep(E2E_HOME, TEST_IFACE, tracked_pids)
        env["HOME"] = E2E_HOME
        # provision（flow 级，union）— 资源获取失败留空 → 用例 FAIL
        provs = set(s["provision"] for s in steps if s.get("provision"))
        provision(provs, ctx, TEST_IFACE)
        if "pid" in ctx and ctx.get("pid"):
            tracked_pids.append(int(ctx["pid"]))
        if "_watcher_pid" in ctx:
            tracked_pids.append(ctx["_watcher_pid"])
        # SEC-P 类（非 root 拒绝）：inject 步降权到 nobody 验证拒绝；verify/query 仍 root
        is_priv = fid.startswith("SEC-P")

        # 物理前置检查（缺失 → SKIP，非代码问题）。放在 sweep 之后：
        # sweep 已尝试恢复环境，仍不满足说明是物理前置（如 RoCE 无网线）。
        precond = (steps[0].get("precondition") or "none") if steps else "none"
        skip_reason = check_precondition(precond)
        if skip_reason:
            for s in steps:
                results.append(_res(s, "SKIP", skip_reason))
            counters["SKIP"] += 1
            cat_stats.setdefault(cat, {"PASS": 0, "FAIL": 0, "SKIP": 0})
            cat_stats[cat]["SKIP"] += 1
            print(f"  SKIP {fid}: {skip_reason}")
            continue

        flow_pass = True
        for s in steps:
            res = _res(s)
            cmd = substitute(s["command"], ctx)
            t0 = time.time()
            if not cmd:
                # setup/provision step (command 空) → 跳过执行
                res["actual_exit_code"] = ""
                res["result"] = "PASS"
                res["notes"] = "provision/setup"
                results.append(res)
                continue
            priv = "nobody" if (is_priv and s["phase"] == "inject") else None
            rc, so, se = run_step_cmd(cmd, env=env, dcat_bin=args.dcat, timeout=120, priv_user=priv)
            out = (so or "") + (se or "")
            dt = int((time.time() - t0) * 1000)
            res["actual_exit_code"] = rc
            res["actual_json"] = out.strip()[:300]
            res["duration_ms"] = dt
            res["timestamp"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            ok, detail, vout, vrc = _eval_step(s, rc, out, ctx, env)
            res["verify_actual"] = detail
            res["result"] = "PASS" if ok else "FAIL"
            res["error_code"] = rc
            results.append(res)
            if not ok:
                flow_pass = False
                _log_failure(s, rc, so, se, vout, vrc, detail, ctx, fail_log_path)
                if cat == "SEC":
                    findings.append(f"{s['id']} ({fid}): {s['expected_behavior']} → {detail}")
                break  # flow 内一步失败则中止该 flow 后续
        # 后置清扫
        if "pid" in ctx and ctx.get("pid"):
            pid = int(ctx["pid"])
            if pid in tracked_pids:
                tracked_pids.remove(pid)
        wpid = ctx.get("_watcher_pid")
        kills = []
        if "pid" in ctx and ctx.get("pid"):
            kills.append(int(ctx["pid"]))
        if wpid:
            kills.append(wpid)
        sweep(E2E_HOME, TEST_IFACE, kills)
        # 回收 watcher 僵尸（本框架的直系子进程）；target 是 watcher 的子，init 回收
        if wpid:
            try:
                os.waitpid(wpid, 0)
            except OSError:
                pass
            if wpid in tracked_pids:
                tracked_pids.remove(wpid)
        # 清理 loop/docker 临时资源
        if ctx.get("loop_dev"):
            sh(f"losetup -d {ctx['loop_dev']} 2>/dev/null")
            sh("rm -f /tmp/dcat-e2e-loop.img 2>/dev/null")
        if ctx.get("ctr"):
            sh(f"docker rm -f {ctx['ctr']} 2>/dev/null")
        counters["PASS" if flow_pass else "FAIL"] += 1
        cat_stats.setdefault(cat, {"PASS": 0, "FAIL": 0, "SKIP": 0})
        cat_stats[cat]["PASS" if flow_pass else "FAIL"] += 1
        print(f"  {'PASS' if flow_pass else 'FAIL'} {fid}")

    # ---- 输出 results csv ----
    rpath = os.path.join(args.out_dir, f"results_{ts}.csv")
    with open(rpath, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=RESULT_COLS)
        w.writeheader()
        for r in results:
            w.writerow({k: r.get(k, "") for k in RESULT_COLS})
    print(f"[phase] results csv written: {rpath}", flush=True)

    # ---- report.md ----
    rep = os.path.join(args.out_dir, "report.md")
    _write_report(rep, results, counters, cat_stats, findings, ts, ipt, args.dcat)
    print("[phase] report.md written", flush=True)
    # ---- append test_report.md §10 ----
    if not args.no_append:
        _append_test_report(args.report, results, counters, cat_stats, findings, ts, ipt, args.dcat)
        print("[phase] test_report.md appended", flush=True)

    total = sum(counters.values())
    fails = [r for r in results if r["result"] == "FAIL"]
    print("\n========== E2E 汇总 ==========")
    print(f"  PASS {counters['PASS']}  FAIL {counters['FAIL']}  SKIP {counters['SKIP']}  TOTAL {total}")
    print(f"  results:  {rpath}")
    print(f"  report:   {rep}")
    print(f"  failures: {fail_log_path}")
    if fails:
        print(f"\n----- 失败用例摘要 ({len(fails)}) -----")
        for r in fails:
            print(f"  [FAIL] {r['id']} | {r['flow_id']} | step {r['step']} | {r['phase']} | "
                  f"exit={r['actual_exit_code']} | {r['verify_actual']}")
        print(f"\n完整失败详情(stdout/stderr/verify 输出)见上方控制台输出及 artifact: "
              f"{os.path.basename(fail_log_path)}")
    if findings:
        print(f"\n  findings: {len(findings)}")
        for x in findings:
            print(f"    - {x}")
    _emit_gha_summary(rep, results, counters, cat_stats, fails, rpath, fail_log_path)
    return 1 if counters["FAIL"] else 0


def _res(s, result="", notes=""):
    return dict(id=s["id"], flow_id=s["flow_id"], step=s["step"], phase=s["phase"],
                fault_uid=s["fault_uid"], module=s.get("module", ""),
                command=s["command"], expected_exit_code=s["expected_exit_code"],
                expected_json=s["expected_json"], verify_cmd=s["verify_cmd"],
                verify_assert=s["verify_assert"],
                expected_behavior=s.get("expected_behavior", ""),
                actual_exit_code="", actual_json="", verify_actual="",
                result=result, error_code="", duration_ms="", timestamp="", notes=notes)


def _eval_step(s, rc, out, ctx, env):
    """评估单步：exit_code + expected_json + verify_assert。
    返回 (ok, detail, verify_out, verify_rc) 供调用方记录诊断信息。"""
    exp = s["expected_exit_code"]
    if exp and exp != "*":
        if exp == "nonzero":
            if rc == 0:
                return False, f"expected nonzero, got {rc}", "", 0
        else:
            try:
                if rc != int(exp):
                    return False, f"exit {rc} != {exp}", "", 0
            except ValueError:
                pass
    ej = s["expected_json"]
    if ej and ej not in out:
        return False, f"json missing '{ej}'", "", 0
    vasrt = s["verify_assert"]
    vcmd = substitute(s["verify_cmd"], ctx)
    vout = ""
    vrc = 0
    # 观测前留 settle 时间（注入/清除后系统状态需片刻稳定，如 python listen / dd / kill 生效）
    if vcmd and not vasrt.startswith(("state_", "exitcode:", "out_contains:")):
        time.sleep(0.6)
    if vcmd and not vasrt.startswith(("state_", "exitcode:", "exists:", "notexists:", "out_contains:")):
        vrc, vout = sh(vcmd, env=env, timeout=30)
    elif vcmd and vasrt.startswith(("exists:", "notexists:")):
        vrc, vout = sh(vcmd, env=env, timeout=30)  # e.g. injection flag probe
    ok, detail = apply_assert(vasrt, vout, rc, out)
    return ok, detail, vout, vrc


def _log_failure(s, rc, so, se, vout, vrc, detail, ctx, fail_log_path):
    """打印并记录失败用例详细信息：控制台 + GHA ::error:: 注解 + failures_<ts>.log。
    捕获 command/stdout/stderr/verify_cmd/verify_output/期望vs实际，方便用户定位解决。"""
    ts = datetime.now().strftime("%H:%M:%S")
    cmd = substitute(s["command"], ctx)
    vcmd = substitute(s.get("verify_cmd", ""), ctx)
    # GitHub Actions 注解（单行摘要，显示在 UI Annotations 面板，无需下载 artifact 即可定位）
    summary = (f"flow={s['flow_id']} id={s['id']} step={s['step']} "
               f"phase={s['phase']} uid={s.get('fault_uid', '')}: {detail}")
    print("::error::" + summary.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A'),
          flush=True)
    block = []
    block.append("=" * 72)
    block.append(f"[FAIL] {ts} | flow={s['flow_id']} | id={s['id']} | "
                 f"step={s['step']} | phase={s['phase']}")
    block.append(f"  fault_uid:         {s.get('fault_uid', '')}")
    block.append(f"  expected_behavior: {s.get('expected_behavior', '') or s.get('notes', '')}")
    block.append(f"  command:           {cmd}")
    block.append(f"  expected_exit:     {s.get('expected_exit_code', '')}")
    block.append(f"  expected_json:     {s.get('expected_json', '')}")
    block.append(f"  verify_cmd:        {vcmd}")
    block.append(f"  verify_assert:     {s.get('verify_assert', '')}")
    block.append("  ---- ACTUAL ----")
    block.append(f"  actual_exit_code:  {rc}")
    block.append(f"  stdout:")
    for line in (so or "").splitlines() or ["(empty)"]:
        block.append(f"    {line}")
    block.append(f"  stderr:")
    for line in (se or "").splitlines() or ["(empty)"]:
        block.append(f"    {line}")
    block.append(f"  verify_exit_code:  {vrc}")
    block.append(f"  verify_output:")
    for line in (vout or "").splitlines() or ["(empty)"]:
        block.append(f"    {line}")
    block.append(f"  detail:             {detail}")
    block.append("=" * 72)
    text = "\n".join(block)
    print(text, flush=True)
    try:
        with open(fail_log_path, "a", encoding="utf-8") as f:
            f.write(text + "\n\n")
    except Exception as e:
        print(f"WARN: write failures log failed: {e}", file=sys.stderr)


def _emit_gha_summary(rep, results, counters, cat_stats, fails, rpath, fail_log_path):
    """向 GitHub Actions Job Summary ($GITHUB_STEP_SUMMARY) 写入摘要。
    标题含 arch(x86_64/aarch64) 区分 x86/arm 矩阵腿；列出具体通过/失败 flow_id。
    本地运行(无该环境变量)时自动 no-op。"""
    gss = os.environ.get("GITHUB_STEP_SUMMARY")
    if not gss:
        return
    import platform
    arch = platform.machine()  # x86_64 / aarch64
    total = sum(counters.values())
    rate = counters["PASS"] * 100 // max(1, total)
    # 按 flow 聚合：该 flow 任一步 FAIL → FAIL，否则 PASS
    flow_state = {}
    for r in results:
        fid = r["flow_id"]
        if r["result"] == "FAIL":
            flow_state[fid] = "FAIL"
        elif fid not in flow_state:
            flow_state[fid] = "PASS"
    pass_flows = sorted(f for f, s in flow_state.items() if s == "PASS")
    fail_flows = sorted(f for f, s in flow_state.items() if s == "FAIL")
    lines = []
    lines.append(f"## E2E 摘要 [{arch}]\n")
    lines.append(f"**PASS {counters['PASS']} / FAIL {counters['FAIL']} / "
                 f"SKIP {counters['SKIP']} / TOTAL {total}** — 通过率 **{rate}%**\n")
    if pass_flows:
        lines.append(f"\n<details><summary><b>通过用例 ({len(pass_flows)})</b></summary>\n\n")
        lines.append(", ".join(f"`{f}`" for f in pass_flows))
        lines.append("\n\n</details>\n")
    if fail_flows:
        lines.append(f"\n<details><summary><b>失败用例 ({len(fail_flows)})</b></summary>\n\n")
        lines.append(", ".join(f"`{f}`" for f in fail_flows))
        lines.append("\n\n</details>\n")
        lines.append("\n| id | flow | step | phase | exit | detail |")
        lines.append("|---|---|---|---|---|---|")
        for r in fails:
            d = (r.get("verify_actual") or "").replace("|", "\\|")[:80]
            lines.append(f"| {r['id']} | {r['flow_id']} | {r['step']} | {r['phase']} | "
                         f"{r['actual_exit_code']} | {d} |")
        lines.append(f"\n> 完整失败详情见 artifact: `{os.path.basename(fail_log_path)}` "
                     f"及 `{os.path.basename(rep)}`\n")
    try:
        with open(gss, "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    except Exception as e:
        print(f"WARN: write GITHUB_STEP_SUMMARY failed: {e}", file=sys.stderr)


def _flow_purpose(steps):
    """聚合一个 flow 各步骤的 expected_behavior → 测试目的描述。
    跳过 setup/provision 步骤，按执行顺序串联，去重。"""
    purposes = []
    for s in steps:
        if s.get("notes") == "provision/setup" or s.get("phase") == "setup":
            continue
        b = (s.get("expected_behavior") or "").strip()
        if b and b not in purposes:
            purposes.append(b)
    return " → ".join(purposes) if purposes else "(无描述)"


def _write_report(path, results, counters, cat_stats, findings, ts, ipt, dcat_bin):
    lines = []
    lines.append(f"# DemonCAT E2E 测试报告\n\n生成时间: {ts}  |  root: {ipt}  |  dcat: {dcat_bin}\n")
    lines.append(f"## 汇总\n\n| 指标 | 值 |\n|---|---|")
    lines.append(f"| 流程(flow)总数 | {sum(counters.values())} |")
    lines.append(f"| PASS(流程) | {counters['PASS']} |")
    lines.append(f"| FAIL(流程) | {counters['FAIL']} |")
    lines.append(f"| SKIP(流程) | {counters['SKIP']} |")
    lines.append(f"| 执行步骤(step)数 | {len(results)} |")
    lines.append(f"| 通过率(按流程) | {counters['PASS']*100//max(1,sum(counters.values()))}% |")
    lines.append("\n> 说明：汇总按\"流程(flow)\"计数（每 flow 一个 PASS/FAIL），"
                 "步骤明细见下方用例表。两者不等属正常。\n")

    # ---- 测试用例明细（含测试目的）----
    lines.append("## 测试用例明细（含测试目的）\n")
    lines.append("> 下表列出本次实际执行的每个测试流程(flow)，"
                 "测试目的由该 flow 各步骤的 expected_behavior 串联得出，"
                 "展示该用例从头到尾验证了什么。\n")
    lines.append("| # | flow_id | 分类 | 模块 | 测试目的 | 步骤数 | 结果 | 耗时(ms) |"
                 "\n|---|---|---|---|---|---|---|---|")
    flow_rows = {}
    for r in results:
        flow_rows.setdefault(r["flow_id"], []).append(r)
    idx = 0
    for fid in sorted(flow_rows.keys()):
        idx += 1
        steps = flow_rows[fid]
        cat = fid.split("-")[0]
        module = steps[0].get("module", "") if steps else ""
        purpose = _flow_purpose(steps).replace("|", "\\|")
        nsteps = len(steps)
        flow_result = "FAIL" if any(s.get("result") == "FAIL" for s in steps) else "PASS"
        total_ms = sum(int(s.get("duration_ms") or 0) for s in steps)
        lines.append(f"| {idx} | {fid} | {cat} | {module} | {purpose} | "
                     f"{nsteps} | {flow_result} | {total_ms} |")
    lines.append("")

    # ---- 分类统计（含分类说明）----
    lines.append("## 分类统计\n\n| 分类 | 说明 | PASS | FAIL | SKIP |\n|---|---|---|---|---|")
    for cat in sorted(cat_stats):
        c = cat_stats[cat]
        lines.append(f"| {cat} | {CAT_DESC.get(cat, '')} | {c['PASS']} | {c['FAIL']} | {c['SKIP']} |")
    lines.append("\n## 失败/发现\n")
    fails = [r for r in results if r["result"] == "FAIL"]
    if fails:
        lines.append("| id | flow | phase | command | detail | expected_behavior |"
                     "\n|---|---|---|---|---|---|")
        for r in fails:
            eb = (r.get("expected_behavior") or "").replace("|", "\\|")[:60]
            lines.append(f"| {r['id']} | {r['flow_id']} | {r['phase']} | "
                         f"{r['command'][:40]} | {r['verify_actual'][:60]} | {eb} |")
    else:
        lines.append("无失败。\n")
    if findings:
        lines.append("\n## 安全/韧性发现\n")
        for x in findings:
            lines.append(f"- {x}")
    lines.append("\n## 跳过原因\n")
    skips = [r for r in results if r["result"] == "SKIP"]
    seen = set()
    for r in skips:
        key = (r["flow_id"], r["notes"])
        if key not in seen:
            seen.add(key)
            lines.append(f"- {r['flow_id']}: {r['notes']}")
    open(path, "w", encoding="utf-8").write("\n".join(lines))


def _append_test_report(path, results, counters, cat_stats, findings, ts, ipt, dcat_bin):
    total = sum(counters.values())
    rate = counters["PASS"] * 100 // max(1, total)
    sec = []
    sec.append(f"\n\n## 10. E2E 测试（CSV 驱动，{ts}）\n")
    sec.append("> 由 `tests/e2e/run_e2e.py` 生成。串行执行，每例前后幂等清扫环境（dcat 命名空间）。"
               "用例见 `tests/e2e/cases.csv`（`gen_cases.py` 自动生成），结果见 `tests/e2e/results_*.csv`，"
               "逐用例测试目的见 `tests/e2e/report.md` 及下方 10.2 表。\n\n")
    sec.append(f"- 执行环境: root={ipt}, HOME 隔离={E2E_HOME}, 测试网卡={TEST_IFACE}\n")
    sec.append(f"- 结果: **PASS {counters['PASS']} / FAIL {counters['FAIL']} / SKIP {counters['SKIP']} / TOTAL {total}**，通过率 {rate}%\n\n")
    sec.append("### 10.1 分类统计\n\n| 分类 | 说明 | PASS | FAIL | SKIP |\n|---|---|---|---|---|")
    for cat in sorted(cat_stats):
        c = cat_stats[cat]
        sec.append(f"| {cat} | {CAT_DESC.get(cat, '')} | {c['PASS']} | {c['FAIL']} | {c['SKIP']} |")
    # ---- 10.2 测试用例明细（含测试目的）----
    sec.append("\n### 10.2 测试用例明细（含测试目的）\n\n")
    sec.append("| # | flow_id | 分类 | 测试目的 | 步骤数 | 结果 | 耗时(ms) |"
               "\n|---|---|---|---|---|---|---|")
    flow_rows = {}
    for r in results:
        flow_rows.setdefault(r["flow_id"], []).append(r)
    idx = 0
    for fid in sorted(flow_rows.keys()):
        idx += 1
        steps = flow_rows[fid]
        cat = fid.split("-")[0]
        purpose = _flow_purpose(steps).replace("|", "\\|")
        nsteps = len(steps)
        flow_result = "FAIL" if any(s.get("result") == "FAIL" for s in steps) else "PASS"
        total_ms = sum(int(s.get("duration_ms") or 0) for s in steps)
        sec.append(f"| {idx} | {fid} | {cat} | {purpose} | "
                   f"{nsteps} | {flow_result} | {total_ms} |")
    fails = [r for r in results if r["result"] == "FAIL"]
    sec.append("\n### 10.3 覆盖说明\n")
    sec.append("- 生产全量跑，原则上不 skip：root/NPU/硬件依赖用例在缺资源环境会 FAIL（生产应全绿）。\n")
    sec.append("- 例外：rNPU_link_down 在 RoCE 链路物理 DOWN（无网线/对端）时 SKIP（需链路可拉低/恢复，物理前置，非代码问题）。\n")
    sec.append("- SEC-P 类(非 root 拒绝)：inject 步用 `runuser -u nobody` 降权验证拒绝。\n")
    sec.append("- FUNC 中 rCPU_core_offline 默认实跑（瞬态下线真实核 cpu1，clean+清扫恢复）。\n")
    sec.append("- SEC-H 写入边界：用 device=/tmp 安全路径（不污染 /etc）。\n")
    sec.append("- CONC/INTER 为混沌工程新增维度：并发竞争与故障交互。\n")
    if findings:
        sec.append("\n### 10.4 安全/韧性发现\n")
        for x in findings:
            sec.append(f"- {x}")
    if fails:
        sec.append("\n### 10.5 失败用例\n\n| id | flow | phase | detail |\n|---|---|---|---|")
        for r in fails[:30]:
            sec.append(f"| {r['id']} | {r['flow_id']} | {r['phase']} | {r['verify_actual'][:80]} |")
    try:
        with open(path, "a", encoding="utf-8") as f:
            f.write("\n".join(sec) + "\n")
    except Exception as e:
        print(f"WARN: append test_report.md failed: {e}", file=sys.stderr)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        import traceback
        tb = traceback.format_exc()
        msg = f"run_e2e.py crashed: {e}".replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
        print(f"::error::{msg}", flush=True)
        print(tb, file=sys.stderr, flush=True)
        sys.exit(1)
