"""tests/e2e/test_e2e_cases.py — testcases.xlsx 驱动的参数化 e2e 测试。

每个 xlsx 用例 = 一个 pytest item（id=TC-xxx_模块）。
执行流：skip_reason→skip → 前序 setup（「已注入」）→ 按序跑解析出的 dcat 命令
→ 验证观测命令（N/A 则作用于 dcat stdout/exitcode）→ e2e_assert.eval_assert。
"""
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
          "ss ", "for ", "iptables", "hccn_tool", "systemctl", "echo ")


def _is_runnable_vcmd(vcmd):
    return bool(vcmd) and any(k in vcmd for k in _RUNKW) and not any(m in vcmd for m in _NA_MARKERS)


@pytest.mark.parametrize("case", parametrized_cases())
def test_case(case, dcat, e2e_env, autouse_sweep, recorder, tracked):
    recorder.case = case

    # 1. 预置 skip（无 dcat 命令 / setup 不可解析）
    if case.skip_reason:
        recorder.detail = case.skip_reason
        pytest.skip(case.skip_reason)

    # 2. 物理前置（仅 coded 值 roce_link_up 触发；xlsx 多为描述性 = no-op）
    coded_pre = case.precondition if case.precondition in ("none", "roce_link_up") else "none"
    pre_skip = check_precondition(coded_pre)
    if pre_skip:
        recorder.detail = pre_skip
        pytest.skip(pre_skip)

    env = e2e_env["env"]
    ctx = {"iface": e2e_env["iface"], "pid": "", "port": "", "svc": ""}

    t0 = time.time()

    # 3. 前序注入 setup（前置条件「已注入 <module> <args>」且步骤无 inject）
    if case.setup_argv:
        setup_str = shlex_join_dcat(dcat, case.setup_argv)
        run_step_cmd(setup_str, env=env, dcat_bin=dcat, timeout=120)

    # 4. 按序跑解析出的 dcat 命令；末条输出作为断言对象
    last_rc, last_out, last_verb = 0, "", ""
    for argv in case.cmds:
        cmd_str = shlex_join_dcat(dcat, argv)
        rc, so, se = run_step_cmd(cmd_str, env=env, dcat_bin=dcat, timeout=120)
        last_rc = rc
        last_out = (so or "") + (se or "")
        last_verb = argv[1] if len(argv) > 1 else ""
        recorder.cmd_str = cmd_str
        recorder.rc = rc
        recorder.out = last_out
        recorder.phase = last_verb
    recorder.duration_ms = int((time.time() - t0) * 1000)

    vassert = case.vassert
    recorder.vassert = vassert
    cmd_rc, cmd_out = last_rc, last_out

    # 5. 断言所需输入：state_* 需 query 输出；其余用 verify 观测命令
    state_data = []
    confirmed = None
    if vassert and vassert.startswith("state_"):
        if last_verb == "query":
            state_data = state_data_of(last_out)
            confirmed = confirmed_of(last_out)
        else:
            state_data, _rc, qout = query_state(case.module, env=env, dcat_bin=dcat)
            confirmed = confirmed_of(qout)
            recorder.verify_cmd = "(state query)"
            recorder.verify_out = qout
    else:
        if _is_runnable_vcmd(case.vcmd):
            time.sleep(0.6)
            recorder.verify_cmd = case.vcmd
            verify_out = sh(substitute(case.vcmd, ctx), env=env, timeout=30)[1]
            recorder.verify_out = verify_out
        else:
            recorder.verify_cmd = case.vcmd or "(N/A)"

    # 6. 评估
    result, detail = eval_assert(vassert, cmd_rc, cmd_out,
                                 getattr(recorder, "verify_out", ""), state_data, confirmed)
    recorder.detail = detail
    if result == "skip":
        pytest.skip(detail)
    if result == "fail":
        assert False, f"{case.id}: {detail}"
    # pass


def shlex_join_dcat(dcat_bin, argv):
    import shlex
    return shlex.join([dcat_bin] + list(argv[1:]))
