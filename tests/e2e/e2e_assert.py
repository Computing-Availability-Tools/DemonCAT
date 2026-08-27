"""tests/e2e/e2e_assert.py — 扩展断言映射器。

复用 e2e_helpers.apply_assert 的基础算子（exitcode/>=/eq/ne/contains/
notcontains/out_contains/regex/nonempty/exists/notexists:path 等），
新增 testcases.xlsx 超集词表：confirmed:true / confirmed:true→false / status:ok /
state_*:<uid>（占位符 → 退化为非空/空）。
未映射/占位/描述性断言 → 返回 result="skip"，由调用方 pytest.skip()。
"""
from e2e_helpers import apply_assert


def _norm(s):
    """去引号去空白，兼容 compact(status:ok) 与 JSON("status":"ok") 两种形态。"""
    return (s or "").replace('"', '').replace(' ', '')


# 不可自动化的断言（缺省/占位/描述性）→ skip
def _is_skip_value(v):
    if v is None:
        return True
    v = v.strip()
    if not v:
        return True
    # 全角括号开头 = 描述性中文断言，如 （stderr 含告警以上）
    if v.startswith("（"):
        return True
    # 含 < > 占位符但非 state_*（state_*:<uid> 另行处理）
    if "<" in v and ">" in v and not v.startswith("state_"):
        return True
    # 裸 notexists（无路径，无法判定）→ skip（由 _eval_step 转成 notexists:<path> 或 notexists_rc:）
    if v == "notexists":
        return True
    return False


def eval_assert(vassert, cmd_rc, cmd_out, verify_out, state_data, confirmed=None):
    """评估单条断言。

    返回 (result, detail)：result ∈ {'pass','fail','skip'}。
      vassert      xlsx 验证断言原文
      cmd_rc       dcat 命令退出码
      cmd_out      dcat 命令 stdout(+stderr)
      verify_out   验证观测命令输出（N/A 时为 ""）
      state_data   dcat query 解析出的 data[] 列表（state_* 用）
      confirmed    dcat query 返回 confirmed 布尔（部分故障 query 返回
                   {"data":{"confirmed":bool}} 而非记录列表，state_* 据此判定活跃态）
    """
    if _is_skip_value(vassert):
        return ("skip", f"unmapped/placeholder assert: {vassert!r}")

    v = vassert.strip()

    # ---- 新增词表：status / confirmed ----
    if v == "status:ok":
        ok = "status:ok" in _norm(cmd_out)
        return (("pass" if ok else "fail"), f"status:ok in dcat out? {ok}")

    if v.startswith("confirmed:true"):
        if v.endswith("false"):  # confirmed:true→false：clean 后翻转
            ok = "confirmed:true" not in _norm(verify_out)
            return (("pass" if ok else "fail"), f"confirmed flipped to false? {ok}")
        ok = "confirmed:true" in _norm(verify_out)
        return (("pass" if ok else "fail"), f"confirmed:true in verify? {ok}")

    # ---- state_* ----
    if v == "state_empty":
        ok = len(state_data) == 0
        return (("pass" if ok else "fail"), f"state empty? len={len(state_data)}")

    if v.startswith("state_contains:"):
        uid = v.split(":", 1)[1]
        if uid == "<uid>":  # 占位符 → 退化为“state 活跃”
            active = (len(state_data) > 0) or (confirmed is True)
            return (("pass" if active else "fail"),
                    f"state contains <uid>(active)? len={len(state_data)} confirmed={confirmed}")
        ok = any(r.get("uid") == uid for r in state_data)
        return (("pass" if ok else "fail"), f"state contains {uid}? {ok}")

    if v.startswith("state_not_contains:"):
        uid = v.split(":", 1)[1]
        if uid == "<uid>":  # 占位符 → 退化为“state 非活跃”
            inactive = (len(state_data) == 0) and (confirmed is not True)
            return (("pass" if inactive else "fail"),
                    f"state not_contains <uid>(inactive)? len={len(state_data)} confirmed={confirmed}")
        ok = not any(r.get("uid") == uid for r in state_data)
        return (("pass" if ok else "fail"), f"state not_contains {uid}? {ok}")

    # 裸 notexists 且无法定位路径（含通配符/管道）：verify 输出含文件不存在标记即 PASS。
    # （ls <glob> 无匹配 → stderr "ls: cannot access ...  No such file or directory"，退出非 0）
    if v == "notexists_rc:":
        missing = any(k in _norm(verify_out) for k in ("cannotaccess", "nosuchfile", "无法访问", "没有那个文件"))
        return (("pass" if missing else "fail"), f"notexists by ls-error marker: {missing!r}")

    # ---- 委托基础算子 ----
    ok, detail = apply_assert(v, verify_out, cmd_rc, cmd_out)
    if isinstance(detail, str) and detail.startswith("unknown assert"):
        return ("skip", detail)
    return (("pass" if ok else "fail"), detail)
