"""tests/e2e/conftest.py — pytest fixtures + 报告钩子。

复用 e2e_helpers 的 sweep/provision/substitute/check_precondition/RESULT_COLS。
报告产物（与 run_e2e 兼容文件名，run_e2e.py 已退役无冲突）：
  report.md / results_<ts>.csv(RESULT_COLS) / failures_<ts>.log / $GITHUB_STEP_SUMMARY
"""
import csv
import os
import sys
import time
from datetime import datetime
from pathlib import Path

import pytest

HERE = Path(__file__).parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import e2e_helpers
from e2e_helpers import (
    sweep, substitute, check_precondition, run_step_cmd, sh,
    DCAT, TEST_IFACE, E2E_HOME, RESULT_COLS,
)

ROOT = HERE.parent.parent
_wid = os.environ.get("PYTEST_XDIST_WORKER", "")
_suffix = f"_{_wid}" if _wid else ""
_TS = datetime.now().strftime("%Y%m%d_%H%M%S")
_FAIL_LOG = HERE / f"failures_{_TS}{_suffix}.log"
_RESULTS_CSV = HERE / f"results_{_TS}{_suffix}.csv"
_REPORT_MD = HERE / f"report{_suffix}.md"
_RESULTS = []
_NA_MARKERS = ("注入未执行", "无系统断言", "或非故障", "clean后观测")


class Recorder:
    """单次测试的运行记录，供报告钩子在 call 阶段读取。"""

    def __init__(self):
        self.case = None
        self.cmd_str = ""
        self.rc = ""
        self.out = ""
        self.verify_cmd = ""
        self.verify_out = ""
        self.vassert = ""
        self.detail = ""
        self.duration_ms = 0
        self.phase = ""

    def to_row(self, outcome):
        c = self.case
        res = {"passed": "PASS", "failed": "FAIL", "skipped": "SKIP"}.get(outcome, outcome.upper())
        return {
            "id": c.id if c else "",
            "flow_id": c.module if c else "",
            "step": "1",
            "phase": self.phase,
            "fault_uid": c.module if c else "",
            "module": c.module if c else "",
            "command": self.cmd_str,
            "expected_exit_code": "",
            "expected_json": "",
            "verify_cmd": self.verify_cmd,
            "verify_assert": self.vassert,
            "expected_behavior": (c.expected[:80] if c else ""),
            "actual_exit_code": self.rc,
            "actual_json": (self.out or "")[:300],
            "verify_actual": self.detail,
            "result": res,
            "error_code": self.rc if outcome == "failed" else "",
            "duration_ms": self.duration_ms,
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "notes": (c.title if c else ""),
        }


def pytest_configure(config):
    """动态注册 markers（P0/P1/P2 + 各 module_slug + 各 req_slug），
    使 --strict-markers 通过且无 PytestUnknownMarkWarning。"""
    from e2e_loader import load_cases, _req_slug
    registered = set()
    for m in ("P0", "P1", "P2", "smoke", "hardware", "root", "net", "xdist_group"):
        config.addinivalue_line("markers", f"{m}: e2e priority/scope marker")
        registered.add(m)
    try:
        cases = load_cases()
    except Exception:
        cases = []
    for c in cases:
        for m in (c.module_slug, _req_slug(c.req)):
            if m and m not in registered:
                config.addinivalue_line("markers", f"{m}: e2e module/req marker")
                registered.add(m)


# ---------------- fixtures ----------------
@pytest.fixture(scope="session")
def dcat():
    if not os.path.exists(DCAT):
        pytest.skip(f"dcat binary not built: {DCAT} (run: cmake -B build && cmake --build build)", allow_module_level=True)
    return DCAT


@pytest.fixture(scope="session")
def e2e_env():
    os.makedirs(E2E_HOME, exist_ok=True)
    env = dict(os.environ)
    env["HOME"] = E2E_HOME
    env["E2E_HOME"] = E2E_HOME
    is_root = hasattr(os, "geteuid") and os.geteuid() == 0
    auto_sudo = (not is_root) and os.environ.get("DCAT_AUTO_SUDO") == "1"
    sudo = "sudo -n -E " if auto_sudo else ""
    if is_root or auto_sudo:
        e2e_helpers.sh(f"{sudo}ip link add {TEST_IFACE} type dummy 2>/dev/null")
        e2e_helpers.sh(f"{sudo}ip link set {TEST_IFACE} up 2>/dev/null")
    # 会话级一次：改值型 NPU 参数归一化到确定性基线（网关/ip/bw/dscp/netdetect/roce/mtu）。
    # 注入目标值≠基线 → 后续测试注入必然产生真实变更，可无限次重复执行。
    e2e_helpers.npu_normalize_baseline()
    # detect real physical interface for rNET tests (xlsx hardcodes eth0)
    phy_iface = ""
    ok, out = e2e_helpers.sh(
        "for i in $(ls /sys/class/net 2>/dev/null); do "
        "case $i in lo|dummy*|veth*|br*|docker*|dcat*|virbr*) continue;; esac; "
        "s=$(ethtool $i 2>/dev/null | grep -oE 'Speed: [0-9]+'); "
        "[ -n \"$s\" ] && echo $i && exit 0; done; exit 1")
    if ok == 0 and out.strip():
        phy_iface = out.strip().splitlines()[0]
    yield {"env": env, "iface": TEST_IFACE, "is_root": is_root, "phy_iface": phy_iface}
    if is_root or auto_sudo:
        e2e_helpers.sh(f"{sudo}ip link del {TEST_IFACE} 2>/dev/null", timeout=15)


@pytest.fixture
def tracked():
    pids = []
    yield pids


@pytest.fixture(scope="session", autouse=True)
def session_init_sweep(e2e_env):
    """session 开始时 sweep 一次，清除上次运行/崩溃残留。"""
    tracked = []
    sweep(E2E_HOME, TEST_IFACE, tracked)
    phy = e2e_env.get("phy_iface", "")
    if phy:
        sweep(E2E_HOME, phy, tracked)
    yield


_prev_module = None

@pytest.fixture(autouse=True)
def autouse_sweep(e2e_env, tracked, request):
    global _prev_module
    callspec = getattr(request.node, "callspec", None)
    case = callspec.params.get("case") if callspec else None
    cur_mod = case.module if case else ""

    # 判断是否 inject-only（有 inject 无 clean，会残留故障状态）
    has_inject = has_clean = False
    if case:
        for cmd in case.cmds:
            if len(cmd) > 1 and cmd[1] == "inject":
                has_inject = True
            if len(cmd) > 1 and cmd[1] == "clean":
                has_clean = True
        if case.setup_argv and len(case.setup_argv) > 1 and case.setup_argv[1] == "inject":
            has_inject = True

    # 切换模块时 sweep（清除上个模块残留）
    if _prev_module is None or _prev_module != cur_mod:
        sweep(E2E_HOME, TEST_IFACE, tracked)
        phy = e2e_env.get("phy_iface", "")
        if phy:
            sweep(E2E_HOME, phy, tracked)
    _prev_module = cur_mod

    yield

    # kill tracked PIDs（快速）
    for p in tracked:
        try:
            os.kill(p, 9)
        except OSError:
            pass

    # inject-only 测试会残留 → 必须 sweep 清理
    # 测试失败也必须 sweep（clean 可能未执行）
    rep = getattr(request.node, "rep_call", None)
    if (has_inject and not has_clean) or (rep is not None and rep.failed):
        sweep(E2E_HOME, TEST_IFACE, tracked)
        phy = e2e_env.get("phy_iface", "")
        if phy:
            sweep(E2E_HOME, phy, tracked)


@pytest.fixture
def recorder(request):
    rec = Recorder()
    request.node._e2e_rec = rec
    yield rec


# ---------------- 报告钩子 ----------------
@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if report.when == "call":
        item.rep_call = report
    if report.when != "call":
        return
    rec = getattr(item, "_e2e_rec", None)
    status = report.outcome  # passed/failed/skipped
    if rec is not None and rec.case is not None:
        row = rec.to_row(status)
        _RESULTS.append(row)
        if status == "failed":
            _write_failure(rec, report)
    else:
        # 无 recorder（如 session-skip）：记录最小 SKIP 行
        _RESULTS.append({
            "id": item.nodeid, "flow_id": "", "step": "", "phase": "",
            "fault_uid": "", "module": "", "command": "", "expected_exit_code": "",
            "expected_json": "", "verify_cmd": "", "verify_assert": "",
            "expected_behavior": "", "actual_exit_code": "", "actual_json": "",
            "verify_actual": str(report.longrepr)[:200] if report.longrepr else "",
            "result": "SKIP", "error_code": "", "duration_ms": "",
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "notes": status,
        })


def _write_failure(rec, report):
    c = rec.case
    block = ["=" * 72]
    block.append(f"[FAIL] {datetime.now():%H:%M:%S} | id={c.id} | {c.title}")
    block.append(f"  module:            {c.module}")
    block.append(f"  command:           {rec.cmd_str}")
    block.append(f"  verify_cmd:        {rec.verify_cmd}")
    block.append(f"  verify_assert:     {rec.vassert}")
    block.append("  ---- ACTUAL ----")
    block.append(f"  exit_code:         {rec.rc}")
    block.append(f"  dcat_out:          {(rec.out or '')[:400]}")
    block.append(f"  verify_out:        {(rec.verify_out or '')[:400]}")
    block.append(f"  detail:            {rec.detail}")
    block.append(f"  expected_behavior: {c.expected[:200]}")
    block.append("=" * 72)
    try:
        with open(_FAIL_LOG, "a", encoding="utf-8") as f:
            f.write("\n".join(block) + "\n\n")
    except Exception:
        pass
    # GHA 注解
    msg = f"{c.id} ({c.module}): {rec.detail}".replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    print(f"::error::{msg}", flush=True)


def pytest_sessionfinish(session, exitstatus):
    _is_worker = hasattr(session.config, "workerinput")
    _write_results_csv()
    _write_report_md(session)
    if not _is_worker:
        _emit_gha_summary(session)


def _write_results_csv():
    try:
        with open(_RESULTS_CSV, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=RESULT_COLS)
            w.writeheader()
            for r in _RESULTS:
                w.writerow({k: r.get(k, "") for k in RESULT_COLS})
    except Exception as e:
        print(f"[warn] write results csv failed: {e}")


def _stats():
    c = {"PASS": 0, "FAIL": 0, "SKIP": 0}
    for r in _RESULTS:
        key = r.get("result", "SKIP")
        c[key] = c.get(key, 0) + 1
    return c


def _write_report_md(session):
    c = _stats()
    total = sum(c.values())
    rate = c["PASS"] * 100 // max(1, total)
    lines = [f"# DemonCAT E2E 测试报告（pytest）\n",
             f"生成时间: {_TS}  |  dcat: {DCAT}\n",
             "## 汇总\n\n| 指标 | 值 |\n|---|---|",
             f"| 用例总数 | {total} |",
             f"| PASS | {c['PASS']} |",
             f"| FAIL | {c['FAIL']} |",
             f"| SKIP | {c['SKIP']} |",
             f"| 通过率 | {rate}% |\n"]
    fails = [r for r in _RESULTS if r.get("result") == "FAIL"]
    if fails:
        lines.append("## 失败用例\n\n| id | module | command | detail |\n|---|---|---|---|")
        for r in fails:
            d = (r.get("verify_actual") or "").replace("|", "\\|")[:80]
            lines.append(f"| {r['id']} | {r['module']} | {r['command'][:40]} | {d} |")
    skips = [r for r in _RESULTS if r.get("result") == "SKIP"]
    if skips:
        lines.append(f"\n## 跳过用例 ({len(skips)})\n")
        for r in skips[:50]:
            lines.append(f"- {r['id']}: {r.get('verify_actual','')[:80]}")
    try:
        with open(_REPORT_MD, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    except Exception as e:
        print(f"[warn] write report.md failed: {e}")


def _emit_gha_summary(session):
    gss = os.environ.get("GITHUB_STEP_SUMMARY")
    if not gss:
        return
    c = _stats()
    total = sum(c.values())
    rate = c["PASS"] * 100 // max(1, total)
    fails = [r for r in _RESULTS if r.get("result") == "FAIL"]
    lines = [f"## E2E 摘要 [pytest]\n",
             f"**PASS {c['PASS']} / FAIL {c['FAIL']} / SKIP {c['SKIP']} / TOTAL {total}** — 通过率 **{rate}%**\n"]
    if fails:
        lines.append("\n| id | module | detail |\n|---|---|---|")
        for r in fails[:50]:
            d = (r.get("verify_actual") or "").replace("|", "\\|")[:80]
            lines.append(f"| {r['id']} | {r['module']} | {d} |")
    try:
        with open(gss, "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    except Exception:
        pass
