"""tests/e2e/e2e_loader.py — testcases.xlsx 加载器（收集期）。

按表头名读 testcases.xlsx（容错列重排），逐行解析为 Case：
  - dcat 命令：动词锚定正则 dcat (inject|clean|query|list|serve) 提取，
    去 CJK 后 shlex → argv（argv[0]=='dcat'，运行时由 fixture 换成绝对二进制路径）。
  - setup：前置条件含「已注入」且步骤无 inject → 解析 module+--args 构造 setup 注入；
    解析不出 → skip_reason。
  - skip_reason：无 dcat 命令 / setup 不可解析 → 预置 skip。
parametrized_cases() 产出 pytest.param（id=TC-xxx_模块 + markers）。
"""
import re
import shlex
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path

import openpyxl
import pytest

HERE = Path(__file__).parent
XLSX = HERE / "testcases.xlsx"

# 列表头（中文；.py 为 UTF-8，字面量可用）
H_ID = "用例编号"
H_TITLE = "用例标题"
H_PRE = "前置条件"
H_STEPS = "测试步骤"
H_DATA = "测试数据"
H_EXPECT = "预期结果"
H_PRIO = "优先级"
H_REQ = "关联需求"
H_RISK = "补充标识"
H_VCMD = "验证观测命令"
H_VASSERT = "验证断言"

# 动词锚定：只匹配 dcat <已知动词>，且命令为纯 ASCII——遇 CJK/全角即停止
# （避免「（无 --cores）」等描述性括号里的 flag 被误并入命令）
_DCMD_RE = re.compile(r'dcat\s+(?:inject|clean|query|list|serve)\b[\x20-\x7e]+')
# dcat 动词（用于判断步骤是否含 inject）
_INJECT_RE = re.compile(r'dcat\s+inject\b')
_SETUP_KW = "已注入"
_MODULE_RE = re.compile(r'r\w+')
_FLAG_RE = re.compile(r'--[\w=,./\-]+')


@dataclass
class Case:
    id: str
    title: str
    module: str            # 原始模块（标题首段）
    module_slug: str        # ASCII slug（marker 用）
    priority: str
    req: str
    risktag: str
    precondition: str
    steps: str
    expected: str
    test_data: str
    vcmd: str
    vassert: str
    cmds: list = field(default_factory=list)   # list[list[str]] argv, argv[0]=='dcat'
    setup_argv: list = field(default_factory=None)  # list[str] argv 或 None
    skip_reason: str = ""


def _slug(name, sep="_"):
    s = re.sub(r'[^a-z0-9]+', sep, (name or "").lower()).strip(sep)
    return s or "misc"


def _req_slug(req):
    return re.sub(r'[^a-z0-9]+', '', (req or "").lower()) or "noreq"


def _ascii_only(s):
    return "".join(ch for ch in s if ord(ch) < 128)


def _parse_cmds(steps):
    """从测试步骤提取 dcat 命令 argv 列表。argv[0]=='dcat'。"""
    cmds = []
    for m in _DCMD_RE.finditer(steps or ""):
        cleaned = m.group(0).strip()
        if not cleaned:
            continue
        try:
            argv = shlex.split(cleaned)
        except ValueError:
            continue
        if argv and argv[0] == "dcat":
            cmds.append(argv)
    return cmds


def _parse_setup(precondition):
    """前置条件含「已注入」→ 解析 setup inject argv。无/解析不出 → None。"""
    if not precondition or _SETUP_KW not in precondition:
        return None
    mm = _MODULE_RE.search(precondition)
    if not mm:
        return None
    module = mm.group(0)
    flags = _FLAG_RE.findall(precondition)
    return ["dcat", "inject", module, *flags]


def _load_rows():
    wb = openpyxl.load_workbook(XLSX, data_only=True)
    ws = wb.worksheets[0]
    headers = [ws.cell(row=1, column=c).value for c in range(1, ws.max_column + 1)]
    idx = {h: i for i, h in enumerate(headers) if h}

    def g(row, name, default=""):
        i = idx.get(name)
        if i is None:
            return default
        v = ws.cell(row=row, column=i + 1).value
        return v if v is not None else default

    rows = []
    for r in range(2, ws.max_row + 1):
        if not g(r, H_ID):
            continue
        rows.append({h: g(r, h) for h in idx})
    return rows


@lru_cache(maxsize=1)
def load_cases():
    cases = []
    for row in _load_rows():
        steps = row.get(H_STEPS, "")
        cmds = _parse_cmds(steps)
        pre = row.get(H_PRE, "")
        setup_argv = _parse_setup(pre)
        needs_setup = (_SETUP_KW in pre) and not any(
            len(a) > 1 and a[1] == "inject" for a in cmds
        )

        skip_reason = ""
        if not cmds:
            skip_reason = "no dcat command in steps"
        elif needs_setup and not setup_argv:
            skip_reason = f"setup unparsable from precondition: {pre!r}"

        title = row.get(H_TITLE, "")
        module_raw = title.split("-")[0] if title else ""
        cases.append(Case(
            id=str(row.get(H_ID, "")).strip(),
            title=title,
            module=module_raw,
            module_slug=_slug(module_raw),
            priority=str(row.get(H_PRIO, "")).strip(),
            req=str(row.get(H_REQ, "")).strip(),
            risktag=str(row.get(H_RISK, "") or ""),
            precondition=pre,
            steps=steps,
            expected=row.get(H_EXPECT, ""),
            test_data=row.get(H_DATA, ""),
            vcmd=row.get(H_VCMD, ""),
            vassert=row.get(H_VASSERT, ""),
            cmds=cmds,
            setup_argv=setup_argv if (needs_setup and setup_argv) else None,
            skip_reason=skip_reason,
        ))
    return cases


def _marks_for(case):
    marks = []
    if case.priority in ("P0", "P1", "P2"):
        marks.append(getattr(pytest.mark, case.priority))
    marks.append(getattr(pytest.mark, case.module_slug))
    marks.append(getattr(pytest.mark, _req_slug(case.req)))
    return marks


def parametrized_cases():
    return [
        pytest.param(c, id=f"{c.id}_{c.module_slug}", marks=_marks_for(c))
        for c in load_cases()
    ]
