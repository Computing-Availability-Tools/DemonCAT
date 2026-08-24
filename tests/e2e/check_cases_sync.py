#!/usr/bin/env python3
"""tests/e2e/check_cases_sync.py — 校验 testcases.xlsx 结构 + fault catalog 可解析。

替代旧 gen_cases.py+cases.csv 的"生成后 diff"同步检查：新框架用例为手工维护的
xlsx，无生成步骤，故改为结构完整性校验（表头齐全 + 至少 1 数据行 + catalog 可解析）。
stdlib only (zipfile/xml/configparser)，免 openpyxl，适配 pre-commit 环境。
退出码 0 = 通过；非 0 = xlsx 损坏/缺表头/无数据 或 catalog 解析失败。
"""
import configparser
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONF = ROOT / "config" / "demoncat.conf"
XLSX = Path(__file__).resolve().parent / "testcases.xlsx"
NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
EXPECTED_HEADERS = {"用例编号", "用例标题", "前置条件", "测试步骤", "预期结果", "优先级", "关联需求"}


def _cell_text(c):
    t = c.get("t", "")
    if t == "inlineStr":
        isn = c.find(NS + "is")
        return "".join(x.text or "" for x in isn.iter(NS + "t")) if isn is not None else ""
    if t in ("str", ""):
        v = c.find(NS + "v")
        return v.text if v is not None else ""
    return ""


def _col(ref):
    m = re.match(r"[A-Z]+", ref or "")
    if not m:
        return 0
    col = 0
    for ch in m.group():
        col = col * 26 + (ord(ch) - 64)
    return col


def check_xlsx():
    if not XLSX.exists():
        return f"testcases.xlsx 不存在: {XLSX}"
    try:
        z = zipfile.ZipFile(XLSX)
    except zipfile.BadZipFile:
        return "testcases.xlsx 不是有效 zip（文件损坏）"
    headers = set()
    data_rows = set()
    for name in z.namelist():
        if not (name.startswith("xl/worksheets/sheet") and name.endswith(".xml")):
            continue
        ws = ET.fromstring(z.read(name))
        for row in ws.iter(NS + "row"):
            ri = int(row.get("r", "0"))
            cells = [(_col(c.get("r", "")), _cell_text(c)) for c in row]
            if ri == 1:
                headers.update(t for _, t in cells if t)
            elif ri > 1 and any(t.strip() for _, t in cells):
                data_rows.add(ri)
    z.close()
    missing = EXPECTED_HEADERS - headers
    if missing:
        return f"testcases.xlsx 表头缺失: {sorted(missing)}（期望含 {sorted(EXPECTED_HEADERS)}）"
    if not data_rows:
        return "testcases.xlsx 无数据行（仅表头）"
    return ""


def check_catalog():
    if not CONF.exists():
        return f"demoncat.conf 不存在: {CONF}"
    cp = configparser.ConfigParser()
    try:
        cp.read(CONF, encoding="utf-8")
    except Exception as e:
        return f"demoncat.conf 解析失败: {e}"
    uids = [s[len("fault."):] for s in cp.sections() if s.startswith("fault.")]
    if not uids:
        return "demoncat.conf 无 [fault.*] 段"
    return ""


def main():
    errs = []
    for name, fn in (("testcases.xlsx", check_xlsx), ("fault catalog", check_catalog)):
        e = fn()
        if e:
            errs.append(f"[{name}] {e}")
    if errs:
        print("E2E cases integrity check failed:\n" + "\n".join("  - " + x for x in errs), file=sys.stderr)
        return 1
    print("ok: testcases.xlsx 结构完整 + fault catalog 可解析")
    return 0


if __name__ == "__main__":
    sys.exit(main())
