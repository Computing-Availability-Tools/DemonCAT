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

RESULT_COLS = [
    "id", "flow_id", "step", "phase", "fault_uid",
    "command", "expected_exit_code", "expected_json", "verify_cmd", "verify_assert",
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


def run_step_cmd(cmd, env, timeout=120, priv_user=None):
    """dcat 命令用 argv 列表执行（不带 shell），使注入载荷原样进入 dcat cli_parse
    （否则 shell=True 会把 ';touch ...' 当命令分隔符，框架自身执行载荷=假阳性）。
    但 CONC 测试用 & wait 做并发，必须 shell=True。
    辅助 shell 命令(rm/echo/pkill/for/$VAR) 仍用 shell=True。
    priv_user: 非 None 时用 runuser 降权到该用户执行（P 类验证非 root 拒绝）。"""
    cs = cmd.strip()
    dcat_rel = "./build/dcat"
    # CONC 测试含 & wait、SEC-S1 clean 含 ; rm 需要 shell；
    # SEC-I 注入含 ;touch（无空格）必须用 argv 防止载荷执行
    needs_shell = ('&' in cs and 'wait' in cs) or ('; ' in cs)
    if (cs.startswith(DCAT) or cs.startswith(dcat_rel)) and not needs_shell:
        try:
            argv = shlex.split(cs)
            if priv_user:
                # runuser -u <user> -- <argv>；root 专用，免密
                if sh(f"command -v runuser >/dev/null 2>&1")[0] != 0:
                    return 1, "[runuser 未安装，无法降权验证非 root 拒绝]"
                argv = ["runuser", "-u", priv_user, "--"] + argv
            p = subprocess.run(argv, capture_output=True, text=True, env=env,
                               timeout=timeout, cwd=ROOT)
            return p.returncode, (p.stdout or "") + (p.stderr or "")
        except subprocess.TimeoutExpired:
            return 124, "[timeout]"
        except Exception as e:
            return 1, f"[exception {e}]"
    return sh(cs, env=env, timeout=timeout)


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
    hccn_tool -i $c -ip_rule -d dir from ip 192.168.1.100 2>/dev/null
    hccn_tool -i $c -ip_rule -d dir from ip 10.20.10.99 2>/dev/null
    hccn_tool -i $c -route -d address 10.20.11.0 netmask 255.255.255.0 2>/dev/null
    hccn_tool -i $c -route -d address 10.20.12.0 netmask 255.255.255.0 2>/dev/null
    hccn_tool -i $c -ip_route -d ip 10.20.13.0 ip_mask 24 table 100 2>/dev/null
    hccn_tool -i $c -ip_route -d ip 10.20.14.0 ip_mask 24 table 100 2>/dev/null
    hccn_tool -i $c -link -s up 2>/dev/null
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


def substitute(s, ctx):
    if not s:
        return s
    for k in ("pid", "port", "iface", "svc"):
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


# ---------------- 主流程 ----------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", default=CASES)
    ap.add_argument("--dcat", default=DCAT)
    ap.add_argument("--out-dir", default=HERE)
    ap.add_argument("--report", default=os.path.join(ROOT, "test_report.md"))
    ap.add_argument("--no-append", action="store_true")
    ap.add_argument("--flows", default="", help="逗号分隔 flow_id 前缀过滤")
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

    def cleanup_all():
        try:
            print("[phase] atexit cleanup start", flush=True)
            sweep(E2E_HOME, TEST_IFACE, tracked_pids)
            if os.geteuid() == 0:
                sh(f"ip link del {TEST_IFACE} 2>/dev/null", timeout=15)
            print("[phase] atexit cleanup done", flush=True)
        except Exception as e:
            print(f"[phase] atexit cleanup error: {e}", flush=True)

    import atexit
    atexit.register(cleanup_all)

    flow_ids = sorted(flows.keys())
    for fid in flow_ids:
        if filt and not any(fid.startswith(p) or p == fid for p in filt):
            continue
        steps = flows[fid]
        cat = fid.split("-")[0]
        # 不再 skip：生产要求全量跑。资源未就绪(无 hccn_tool/非 root/sysfs 等)→用例自然 FAIL。
        ctx = {}

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
            rc, out = run_step_cmd(cmd, env=env, timeout=120, priv_user=priv)
            dt = int((time.time() - t0) * 1000)
            res["actual_exit_code"] = rc
            res["actual_json"] = out.strip()[:300]
            res["duration_ms"] = dt
            res["timestamp"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            ok, detail = _eval_step(s, rc, out, ctx, env)
            res["verify_actual"] = detail
            res["result"] = "PASS" if ok else "FAIL"
            res["error_code"] = rc
            results.append(res)
            if not ok:
                flow_pass = False
                if cat == "SEC" and not ok:
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
        counters["PASS" if flow_pass else "FAIL"] += 1
        cat_stats.setdefault(cat, {"PASS": 0, "FAIL": 0, "SKIP": 0})
        cat_stats[cat]["PASS" if flow_pass else "FAIL"] += 1
        print(f"  {'PASS' if flow_pass else 'FAIL'} {fid}")

    # ---- 输出 results csv ----
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    rpath = os.path.join(args.out_dir, f"results_{ts}.csv")
    with open(rpath, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=RESULT_COLS)
        w.writeheader()
        for r in results:
            w.writerow({k: r.get(k, "") for k in RESULT_COLS})
    print(f"[phase] results csv written: {rpath}", flush=True)

    # ---- report.md ----
    rep = os.path.join(args.out_dir, "report.md")
    _write_report(rep, results, counters, cat_stats, findings, ts, ipt)
    print("[phase] report.md written", flush=True)
    # ---- append test_report.md §10 ----
    if not args.no_append:
        _append_test_report(args.report, results, counters, cat_stats, findings, ts, ipt)
        print("[phase] test_report.md appended", flush=True)

    total = sum(counters.values())
    print("\n========== E2E 汇总 ==========")
    print(f"  PASS {counters['PASS']}  FAIL {counters['FAIL']}  SKIP {counters['SKIP']}  TOTAL {total}")
    print(f"  results: {rpath}")
    print(f"  report:  {rep}")
    if findings:
        print(f"  ⚠ findings: {len(findings)}")
        for x in findings:
            print(f"    - {x}")
    return 1 if counters["FAIL"] else 0


def _res(s, result="", notes=""):
    return dict(id=s["id"], flow_id=s["flow_id"], step=s["step"], phase=s["phase"],
                fault_uid=s["fault_uid"],
                command=s["command"], expected_exit_code=s["expected_exit_code"],
                expected_json=s["expected_json"], verify_cmd=s["verify_cmd"],
                verify_assert=s["verify_assert"],
                actual_exit_code="", actual_json="", verify_actual="",
                result=result, error_code="", duration_ms="", timestamp="", notes=notes)


def _eval_step(s, rc, out, ctx, env):
    """评估单步：exit_code + expected_json + verify_assert."""
    exp = s["expected_exit_code"]
    if exp and exp != "*":
        if exp == "nonzero":
            if rc == 0:
                return False, f"expected nonzero, got {rc}"
        else:
            try:
                if rc != int(exp):
                    return False, f"exit {rc} != {exp}"
            except ValueError:
                pass
    ej = s["expected_json"]
    if ej and ej not in out:
        return False, f"json missing '{ej}'"
    vasrt = s["verify_assert"]
    vcmd = substitute(s["verify_cmd"], ctx)
    vout = ""
    # 观测前留 settle 时间（注入/清除后系统状态需片刻稳定，如 python listen / dd / kill 生效）
    if vcmd and not vasrt.startswith(("state_", "exitcode:", "out_contains:")):
        time.sleep(0.6)
    if vcmd and not vasrt.startswith(("state_", "exitcode:", "exists:", "notexists:", "out_contains:")):
        vrc, vout = sh(vcmd, env=env, timeout=30)
    elif vcmd and vasrt.startswith(("exists:", "notexists:")):
        vrc, vout = sh(vcmd, env=env, timeout=30)  # e.g. injection flag probe
    ok, detail = apply_assert(vasrt, vout, rc, out)
    return ok, detail


def _write_report(path, results, counters, cat_stats, findings, ts, ipt):
    lines = []
    lines.append(f"# DemonCAT E2E 测试报告\n\n生成时间: {ts}  |  root: {ipt}  |  dcat: {DCAT}\n")
    lines.append(f"## 汇总\n\n| 指标 | 值 |\n|---|---|")
    lines.append(f"| 流程(flow)总数 | {sum(counters.values())} |")
    lines.append(f"| PASS(流程) | {counters['PASS']} |")
    lines.append(f"| FAIL(流程) | {counters['FAIL']} |")
    lines.append(f"| SKIP(流程) | {counters['SKIP']} |")
    lines.append(f"| 执行步骤(step)数 | {len(results)} |")
    lines.append(f"| 通过率(按流程) | {counters['PASS']*100//max(1,sum(counters.values()))}% |")
    lines.append("\n> 说明：`cases.csv` 以\"步骤(step)\"计数（每 flow 含 inject/clean/query 多步），"
                 "报告汇总按\"流程(flow)\"计数（每 flow 一个 PASS/FAIL）。两者不等属正常。\n")
    lines.append("## 分类统计\n\n| 分类 | PASS | FAIL | SKIP |\n|---|---|---|---|")
    for cat in sorted(cat_stats):
        c = cat_stats[cat]
        lines.append(f"| {cat} | {c['PASS']} | {c['FAIL']} | {c['SKIP']} |")
    lines.append("\n## 失败/发现\n")
    fails = [r for r in results if r["result"] == "FAIL"]
    if fails:
        lines.append("| id | flow | phase | command | detail | expected_behavior |\n|---|---|---|---|---|---|")
        for r in fails:
            lines.append(f"| {r['id']} | {r['flow_id']} | {r['phase']} | {r['command'][:40]} | {r['verify_actual'][:60]} | {r.get('notes','')} |")
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


def _append_test_report(path, results, counters, cat_stats, findings, ts, ipt):
    total = sum(counters.values())
    rate = counters["PASS"] * 100 // max(1, total)
    sec = []
    sec.append(f"\n\n## 10. E2E 测试（CSV 驱动，{ts}）\n")
    sec.append("> 由 `tests/e2e/run_e2e.py` 生成。串行执行，每例前后幂等清扫环境（dcat 命名空间）。"
               "用例见 `tests/e2e/cases.csv`（`gen_cases.py` 自动生成），结果见 `tests/e2e/results_*.csv`。\n\n")
    sec.append(f"- 执行环境: root={ipt}, HOME 隔离={E2E_HOME}, 测试网卡={TEST_IFACE}\n")
    sec.append(f"- 结果: **PASS {counters['PASS']} / FAIL {counters['FAIL']} / SKIP {counters['SKIP']} / TOTAL {total}**，通过率 {rate}%\n\n")
    sec.append("### 10.1 分类统计\n\n| 分类 | 说明 | PASS | FAIL | SKIP |\n|---|---|---|---|---|")
    desc = {"FUNC": "功能基线(37故障全链路+query<uid>+插件)",
            "BOUND": "边界值(每参数类型系统覆盖)",
            "SEC": "安全(命令注入+权限边界+主机安全+symlink)",
            "STATE": "状态一致性/幂等(clean×2/--force/reinject/并发inject)",
            "RES": "韧性/自愈(state丢失/损坏/孤儿/幽灵/clean--all/state表满)",
            "CLI": "CLI接口(解析错误+帮助+退出码+--config)",
            "CONC": "并发竞争(同时inject+clean/双进程写state)",
            "INTER": "故障交互(多故障叠加/clean一个不影响其他)"}
    for cat in sorted(cat_stats):
        c = cat_stats[cat]
        sec.append(f"| {cat} | {desc.get(cat,'')} | {c['PASS']} | {c['FAIL']} | {c['SKIP']} |")
    fails = [r for r in results if r["result"] == "FAIL"]
    sec.append("\n### 10.2 覆盖说明\n")
    sec.append("- 生产全量跑，不 skip：root/NPU/硬件依赖用例在缺资源环境会 FAIL（生产应全绿）。\n")
    sec.append("- SEC-P 类(非 root 拒绝)：inject 步用 `runuser -u nobody` 降权验证拒绝。\n")
    sec.append("- FUNC 中 rCPU_core_offline 默认实跑（瞬态下线真实核 cpu1，clean+清扫恢复）。\n")
    sec.append("- SEC-H 写入边界：用 device=/tmp 安全路径（不污染 /etc）。\n")
    sec.append("- CONC/INTER 为混沌工程新增维度：并发竞争与故障交互。\n")
    if findings:
        sec.append("\n### 10.3 安全/韧性发现\n")
        for x in findings:
            sec.append(f"- {x}")
    if fails:
        sec.append("\n### 10.4 失败用例\n\n| id | flow | phase | detail |\n|---|---|---|---|")
        for r in fails[:30]:
            sec.append(f"| {r['id']} | {r['flow_id']} | {r['phase']} | {r['verify_actual'][:80]} |")
    try:
        with open(path, "a", encoding="utf-8") as f:
            f.write("\n".join(sec) + "\n")
    except Exception as e:
        print(f"WARN: append test_report.md failed: {e}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
