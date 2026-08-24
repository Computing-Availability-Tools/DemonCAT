"""tests/e2e/e2e_state.py — dcat 状态查询 helper。

跑 dcat query <module>，解析 JSON data[]，供 state_* 断言使用。
复用 e2e_helpers.run_step_cmd（argv 执行）；JSON 解析用 extract_json
（dcat query 输出常为「人类可读前缀 + 末行 JSON」多行形态）。
"""
import json
import re

from e2e_helpers import run_step_cmd


def extract_json(out):
    """从多行输出中提取 JSON 对象。

    dcat query 常输出：人类可读表格/列表若干行 + 最后一行 {...} JSON。
    直接 json.loads(整段) 会失败，故找最后一个以 { 开头的行解析；
    退路：正则取最后一个 {...}；再退路：整段 json.loads。
    """
    if not out:
        return None
    text = out.strip()
    for line in reversed(text.splitlines()):
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except Exception:
                pass
    m = re.findall(r"\{[\s\S]*\}", text)
    for blob in reversed(m):
        try:
            return json.loads(blob)
        except Exception:
            continue
    try:
        return json.loads(text)
    except Exception:
        return None


def state_data_of(out):
    """返回 query 输出中的 data[] 列表；非列表（如 confirmed dict）→ []。"""
    d = extract_json(out)
    if isinstance(d, dict):
        data = d.get("data")
        if isinstance(data, list):
            return data
    return []


def confirmed_of(out):
    """返回 query 输出中的 confirmed 布尔（{data:{confirmed:bool}}）；无则 None。"""
    d = extract_json(out)
    if isinstance(d, dict):
        data = d.get("data")
        if isinstance(data, dict) and "confirmed" in data:
            return bool(data["confirmed"])
    return None


def query_state(module, env=None, dcat_bin=None, timeout=30):
    """跑 `dcat query <module>`，返回 (data_list, rc, out)。"""
    bin_path = dcat_bin
    cmd = f"{bin_path} query {module}"
    rc, out, err = run_step_cmd(cmd, env=env, dcat_bin=bin_path, timeout=timeout)
    full = (out or "") + (err or "")
    return state_data_of(full), rc, full
