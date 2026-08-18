#!/usr/bin/env python3
"""解析 ctest JUnit XML (build/ctest-junit.xml) 生成结构化单元测试摘要:
- markdown 总览表(总数/通过/失败/跳过/耗时/通过率)+ 失败用例详情,写入 $GITHUB_STEP_SUMMARY
  (Actions run 详情 + PR checks 即见,无需翻作业日志)。
- 每个失败用例 emit `::error file=...,line=...::msg` 工作流注解 → 内联出现在 PR Files changed / run Annotations。
本地运行(无 GITHUB_ACTIONS/GITHUB_STEP_SUMMARY)→ markdown 打到 stdout,不写 job summary。

用法: python3 tests/ctest_summary.py [build/ctest-junit.xml]
"""
import os
import re
import sys
import platform
import xml.etree.ElementTree as ET

JUNIT_DEFAULT = "build/ctest-junit.xml"

# 自定义框架(test.h)断言输出形如 "ASSERT_TRUE fail: file.c:NN"、"INT_EQ fail: ... file.c:NN"
_FILE_LINE_RE = re.compile(r"([^\s:]+\.c):(\d+)")


def _fnum(node, attr, default=0.0):
    try:
        return float(node.get(attr, default) or default)
    except (TypeError, ValueError):
        return default


def _inum(node, attr, default=0):
    try:
        return int(node.get(attr, default) or default)
    except (TypeError, ValueError):
        return default


def _text(node):
    return (node.text if node is not None else "") or ""


def _fmt_duration(seconds):
    try:
        s = float(seconds)
    except (TypeError, ValueError):
        return "-"
    if s >= 60:
        m = int(s // 60)
        return f"{m}m {s - m * 60:05.2f}s"
    return f"{s:.2f}s"


def _iter_testsuites(root):
    """yield 所有 <testsuite>(兼容 root 为 <testsuite> 或 <testsuites>)。"""
    if root is None:
        return []
    if root.tag == "testsuite":
        return [root]
    return list(root.iter("testsuite"))


def parse_junit(junit):
    """解析 JUnit XML,返回汇总 dict。
    键: tests,failures,skipped,passed,time,failures_list(name,time,file,line,detail)。
    文件缺失/解析失败 → 空 结果(failures_list=[]),并设 _error。
    """
    res = {"tests": 0, "failures": 0, "skipped": 0, "passed": 0, "time": 0.0,
           "failures_list": []}
    if not junit or not os.path.exists(junit):
        res["_error"] = "no-junit"
        return res
    try:
        root = ET.parse(junit).getroot()
    except Exception as e:
        res["_error"] = f"parse-failed: {e}"
        return res

    suites = _iter_testsuites(root)
    if not suites:
        res["_error"] = "no-suite"
        return res

    total_t = total_f = total_s = total_d = 0
    total_time = 0.0
    failures_list = []
    for ts in suites:
        total_t += _inum(ts, "tests")
        total_f += _inum(ts, "failures")
        total_s += _inum(ts, "skipped")
        total_d += _inum(ts, "disabled")
        total_time += _fnum(ts, "time")
        for tc in ts.findall("testcase"):
            fail = tc.find("failure")
            if fail is None:  # CTest 某些版本失败信息在 <error> 里
                fail = tc.find("error")
            if fail is None:
                continue
            name = tc.get("name", "?")
            time = _fnum(tc, "time")
            detail = _text(fail).strip()
            if not detail:
                sout = _text(tc.find("system-out")).strip()
                serr = _text(tc.find("system-err")).strip()
                detail = "\n".join(x for x in (sout, serr) if x)
            m = _FILE_LINE_RE.search(detail)
            file = m.group(1) if m else ""
            line = int(m.group(2)) if m else 0
            failures_list.append({"name": name, "time": time, "file": file,
                                  "line": line, "detail": detail})
    skipped = total_s + total_d
    passed = max(total_t - total_f - skipped, 0)
    res.update({"tests": total_t, "failures": total_f, "skipped": skipped,
                "passed": passed, "time": total_time, "failures_list": failures_list})
    return res


def render_markdown(parsed, arch):
    """渲染结构化 markdown 摘要(总览表 + 失败用例详情)。"""
    out = [f"## Unit tests (ctest) · {arch}\n"]
    err = parsed.get("_error")
    if err:
        if err == "no-junit":
            out.append("_(无 junit xml:ctest 未运行或未产出结果)_\n")
        else:
            out.append(f"_(junit 解析失败: {err})_\n")
        return "\n".join(out)

    tests = parsed["tests"]
    passed = parsed["passed"]
    failures = parsed["failures"]
    skipped = parsed["skipped"]
    dur = _fmt_duration(parsed["time"])
    rate = (passed / tests * 100.0) if tests else 0.0
    out.append("| 总数 | 通过 | 失败 | 跳过 | 耗时 | 通过率 |")
    out.append("|---:|---:|---:|---:|---:|---:|")
    out.append(f"| {tests} | {passed} | {failures} | {skipped} | {dur} | {rate:.1f}% |\n")

    fl = parsed["failures_list"]
    if not fl:
        out.append("所有用例通过 ✓\n")
        return "\n".join(out)

    out.append(f"### 失败用例 ({len(fl)})\n")
    out.append("| 用例 | 耗时 | 位置 |")
    out.append("|---|---:|---|")
    for f in fl:
        loc = f"{f['file']}:{f['line']}" if f["file"] else "-"
        out.append(f"| `{f['name']}` | {_fmt_duration(f['time'])} | `{loc}` |")
    out.append("")
    out.append("<details><summary><b>失败详情</b></summary>\n")
    for f in fl:
        loc = f"{f['file']}:{f['line']}" if f["file"] else ""
        head = f"`{f['name']}`"
        if loc:
            head += f" · {loc}"
        out.append(f"\n<details><summary>{head}</summary>\n")
        out.append("```")
        detail = f["detail"]
        if len(detail) > 1500:
            detail = detail[:1500] + " …"
        out.append(detail)
        out.append("```")
        out.append("</details>")
    out.append("\n</details>")
    out.append("\n> 完整输出见 artifact `ctest-logs`（build/ctest_run.log + Testing/Temporary/）")
    return "\n".join(out)


def render_annotations(parsed):
    """为每个失败用例生成 `::error file=...,line=...,title=...::msg` 工作流注解(单行)。"""
    anns = []
    for f in parsed["failures_list"]:
        title = f["name"]
        msg = f["detail"].splitlines()[0] if f["detail"] else "failed"
        if f["file"] and f["line"]:
            anns.append(f"::error file={f['file']},line={f['line']},title={title}::{msg}")
        else:
            anns.append(f"::error title={title}::{msg}")
    return anns


def main():
    junit = sys.argv[1] if len(sys.argv) > 1 else JUNIT_DEFAULT
    arch = platform.machine() or "unknown"
    parsed = parse_junit(junit)
    md = render_markdown(parsed, arch)

    print(md)  # 始终打到 stdout(本地/日志可见)
    if os.environ.get("GITHUB_ACTIONS") == "true":
        for a in render_annotations(parsed):
            print(a)
    gss = os.environ.get("GITHUB_STEP_SUMMARY")
    if gss:
        try:
            with open(gss, "a", encoding="utf-8") as fh:
                fh.write(md + "\n")
        except Exception as e:
            print(f"WARN: write GITHUB_STEP_SUMMARY failed: {e}", file=sys.stderr)
    return 0  # 摘要为 best-effort 报告:不叠加失败(ctest 步已红)


if __name__ == "__main__":
    sys.exit(main())
