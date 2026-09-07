"""Unit tests for the E2E performance guards (PR1).

Covers:
  - npu_info() session cache: no repeated hccn_tool probes after first call
  - npu_residue_present(): state.json / sidecar / stress-process residue gate
  - sweep scope: npu scope strips non-NPU cleanup blocks (dmsetup/losetup/iptables/...)

Pure Python, no hardware/hccn_tool required — runnable on developer machines and
CI ubuntu runners (pytest only).
"""
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import e2e_helpers as H


@pytest.fixture(autouse=True)
def _reset_npu_info_cache(monkeypatch):
    monkeypatch.setattr(H, "_NPU_INFO", None)


def test_npu_info_cached_single_probe(monkeypatch):
    calls = []

    def fake_sh(cmd, env=None, timeout=60):
        calls.append(cmd)
        if "command -v hccn_tool" in cmd:
            return (0, "")
        if "ls /dev/davinci" in cmd:
            return (0, "2\n")
        if "-status -g" in cmd:
            return (0, "Settings for eth_ctrl3\n")
        return (1, "")

    monkeypatch.setattr(H, "sh", fake_sh)
    info1 = H.npu_info()
    probe_count = len(calls)
    info2 = H.npu_info()
    assert info1 is info2
    assert len(calls) == probe_count
    assert info1["has_npu"] is True
    assert info1["hccn_ok"] is True
    assert info1["chip"] == "2"
    assert info1["dev"] == "eth_ctrl3"
    assert info1["roce_ok"] is True


def test_npu_info_no_hardware(monkeypatch):
    monkeypatch.setattr(H, "sh", lambda cmd, env=None, timeout=60: (1, ""))
    info = H.npu_info()
    assert info["has_npu"] is False
    assert info["hccn_ok"] is False
    assert info["chip"] == ""
    assert info["dev"] == ""


def test_residue_state_json(monkeypatch, tmp_path):
    home = tmp_path
    (home / ".demoncat").mkdir()
    (home / ".demoncat" / "state.json").write_text('{"data": [{"uid": "rNPU_gw_change"}]}')
    monkeypatch.setattr(H, "sh", lambda *a, **k: (1, ""))
    assert H.npu_residue_present(str(home), tmp_root=str(tmp_path / "t")) is True


def test_residue_sidecar(monkeypatch, tmp_path):
    t = tmp_path / "t"
    t.mkdir()
    (t / "dcat-rNPU_gw_change-2.bak").write_text("orig")
    monkeypatch.setattr(H, "sh", lambda *a, **k: (1, ""))
    assert H.npu_residue_present(str(tmp_path), tmp_root=str(t)) is True


def test_residue_stress_process(monkeypatch, tmp_path):
    monkeypatch.setattr(H, "sh", lambda *a, **k: (0, ""))
    assert H.npu_residue_present(str(tmp_path), tmp_root=str(tmp_path / "t")) is True


def test_no_residue(monkeypatch, tmp_path):
    monkeypatch.setattr(H, "sh", lambda *a, **k: (1, ""))
    assert H.npu_residue_present(str(tmp_path), tmp_root=str(tmp_path / "t")) is False


def test_sweep_script_npu_scope_guards_full_blocks():
    full = H._sweep_script("/h", "if0", "full")
    npu = H._sweep_script("/h", "if0", "npu")
    guard = 'if [ "$SCOPE" = full ]'
    assert 'SCOPE="npu"' in npu
    assert 'SCOPE="full"' in full
    assert full.count(guard) == 2
    assert npu.count(guard) == 2
    # NPU 清理块在所有 scope 生效（守卫外）
    assert "hccn_tool" in npu
    assert "_npu_stress" in npu
    # full-only 清理（dmsetup/losetup/iptables/tc qdisc）位于第二个守卫之后，
    # 即 npu scope 下运行时被 `if [ "$SCOPE" = full ]` 跳过
    second_guard = npu.rindex(guard)
    for term in ("dmsetup", "losetup", "iptables", "tc qdisc"):
        assert npu.index(term) > second_guard


def test_sweep_scope_invalid_defaults_full():
    assert 'SCOPE="full"' in H._sweep_script("/h", "if0", "full")
    assert "dmsetup" in H._sweep_script("/h", "if0", "full")
