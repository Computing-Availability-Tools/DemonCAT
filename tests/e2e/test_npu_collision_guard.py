"""Unit tests for the NPU change-value collision guard in test_e2e_cases.py.

Regression: when a case has no prior-injected setup (setup_argv=None), the call
site built all_injects=[None]+cmds. _npu_target_collisions then raised TypeError
on len(None), which the outer try/except swallowed → coll=[] → the guard silently
checked NOTHING (not even cmds), so machine drift turned a graceful SKIP into a
hard "注入回读校验失败" FAIL.
"""
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import test_e2e_cases as T


@pytest.fixture
def npu_ctx(monkeypatch):
    """ctx with chip set and hccn_tool present, so guard past the early returns."""
    import shutil

    monkeypatch.setattr(shutil, "which", lambda name: "/usr/bin/hccn_tool" if name == "hccn_tool" else None)
    return {"chip": "2", "phy_iface": "", "iface": "dummy0"}


def _inject_gw(uid="rNPU_gw_change"):
    return ["dcat", "inject", uid, "--gateway=10.0.0.1"]


def test_collision_guard_skips_none_and_checks_cmds(npu_ctx, monkeypatch):
    # setup_argv=None 的用例：all_injects 含 None 首位，但仍必须检查 cmds 里的注入
    monkeypatch.setattr(T, "sh", lambda cmd, timeout=15: (0, "gateway:10.0.0.1\n"))
    cmds = [_inject_gw(), ["dcat", "query", "rNPU_gw_change"]]
    hits = T._npu_target_collisions(npu_ctx, [None] + cmds)
    assert ("rNPU_gw_change", "10.0.0.1", "10.0.0.1") in hits


def test_collision_guard_no_collision_with_none(npu_ctx, monkeypatch):
    # 无碰撞（目标值 != 当前值）→ []，即使含 None 也不抛异常
    monkeypatch.setattr(T, "sh", lambda cmd, timeout=15: (0, "gateway:10.0.0.100\n"))
    hits = T._npu_target_collisions(npu_ctx, [None, _inject_gw()])
    assert hits == []
