#!/usr/bin/env python3
"""解析 ctest JUnit XML (build/ctest-junit.xml) 生成单元测试摘要，写入
$GITHUB_STEP_SUMMARY（标题含 arch 区分 x86/arm；失败时列出失败用例名+错误详情）。
本地运行（无 GITHUB_STEP_SUMMARY）时 no-op。

用法: python3 scripts/ctest_summary.py [build/ctest-junit.xml]
"""
import os
import sys
import platform
import xml.etree.ElementTree as ET

JUNIT_DEFAULT = "build/ctest-junit.xml"


def _text(node):
    return (node.text if node is not None else "") or ""


def main():
    gss = os.environ.get("GITHUB_STEP_SUMMARY")
    if not gss:
        return 0  # 本地运行 no-op
    junit = sys.argv[1] if len(sys.argv) > 1 else JUNIT_DEFAULT
    arch = platform.machine()  # x86_64 / aarch64
    out = [f"## Unit tests (ctest) [{arch}]\n"]

    if not os.path.exists(junit):
        out.append("_(无 junit xml：ctest 未运行或未产出结果)_\n")
    else:
        try:
            root = ET.parse(junit).getroot()
        except Exception as e:
            out.append(f"_(junit 解析失败: {e})_\n")
            root = None
        if root is not None:
            ts = root if root.tag == "testsuite" else root.find("testsuite")
            if ts is None:
                ts = root.find(".//testsuite")
            if ts is not None:
                tests = int(ts.get("tests", 0) or 0)
                failures = int(ts.get("failures", 0) or 0)
                skipped = int(ts.get("skipped", 0) or 0)
                disabled = int(ts.get("disabled", 0) or 0)
                passed = tests - failures - skipped - disabled
                out.append(f"**TOTAL {tests} / PASS {passed} / FAIL {failures}"
                           f" / SKIP {skipped + disabled}**\n")
                fails = [tc for tc in ts.iter("testcase")
                         if tc.find("failure") is not None]
                if fails:
                    out.append(f"\n<details><summary><b>失败用例 ({len(fails)})</b>"
                               f"</summary>\n\n")
                    for tc in fails:
                        name = tc.get("name", "?")
                        fail = tc.find("failure")
                        msg = _text(fail).strip()
                        sout = _text(tc.find("system-out")).strip()
                        detail = msg or sout
                        if len(detail) > 800:
                            detail = detail[:800] + " …"
                        out.append(f"- `{name}`\n")
                        out.append(f"  ```\n  {detail}\n  ```\n")
                    out.append("\n</details>\n")
                    out.append("\n> 完整输出见 artifact `ctest-logs`"
                               "（build/ctest_run.log + Testing/Temporary/）\n")
                else:
                    out.append("\n所有用例通过 ✓\n")
    try:
        with open(gss, "a", encoding="utf-8") as fh:
            fh.write("\n".join(out) + "\n")
    except Exception as e:
        print(f"WARN: write GITHUB_STEP_SUMMARY failed: {e}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
