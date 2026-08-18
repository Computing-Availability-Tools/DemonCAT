#!/usr/bin/env python3
"""解析 ctest JUnit XML (build/ctest-junit.xml) + DCAT_SUBTEST 行 (build/ctest_run.log)
生成结构化单元测试摘要:
- markdown 总览表(可执行级:总数/通过/失败/跳过/耗时/通过率)+ 失败用例详情
- 场景级总览(hermetic:解析 DCAT_SUBTEST|<tier>|<name>|<status>| 行,按 tier 分层)
- 每个失败用例 emit `::error file=...,line=...::msg` 工作流注解
本地运行(无 GITHUB_ACTIONS)→ markdown 打到 stdout。

用法: python3 tests/ctest_summary.py [build/ctest-junit.xml [build/ctest_run.log]]
环境: DCAT_TEST_MODE=regular|full(标题标识;空=regular)
"""
import os
import re
import sys
import platform
import xml.etree.ElementTree as ET

JUNIT_DEFAULT = "build/ctest-junit.xml"
LOG_DEFAULT = "build/ctest_run.log"

_FILE_LINE_RE = re.compile(r"([^\s:]+\.c):(\d+)")
_SUBTEST_RE = re.compile(r"^(?:\d+:\s*)?DCAT_SUBTEST\|([^|]*)\|([^|]*)\|([^|]*)(?:\|(.*))?$")


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
    if root is None:
        return []
    if root.tag == "testsuite":
        return [root]
    return list(root.iter("testsuite"))


def parse_junit(junit):
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
            if fail is None:
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


def parse_subtests(log):
    """解析 ctest_run.log 的 DCAT_SUBTEST|<tier>|<name>|<status>|<detail> 行。
    返回 {tiers: {tier: {total,pass,fail}}, list: [{tier,name,status,detail}]}。
    无 log/无行 → 空(tiers={}, list=[])。"""
    out = {"tiers": {}, "list": []}
    if not log or not os.path.exists(log):
        return out
    try:
        with open(log, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = _SUBTEST_RE.match(line.strip())
                if not m:
                    continue
                tier, name, status, detail = (m.group(1), m.group(2),
                                              m.group(3), m.group(4) or "")
                t = out["tiers"].setdefault(tier, {"total": 0, "pass": 0, "fail": 0})
                t["total"] += 1
                if status == "FAIL":
                    t["fail"] += 1
                elif status == "PASS":
                    t["pass"] += 1
                out["list"].append({"tier": tier, "name": name,
                                    "status": status, "detail": detail})
    except Exception:
        pass
    return out


def render_markdown(parsed, subtests, arch, mode):
    out = [f"## Unit tests (ctest·{mode}) · {arch}\n"]
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
    out.append("### 可执行级(ctest entry)\n")
    out.append("| 总数 | 通过 | 失败 | 跳过 | 耗时 | 通过率 |")
    out.append("|---:|---:|---:|---:|---:|---:|")
    out.append(f"| {tests} | {passed} | {failures} | {skipped} | {dur} | {rate:.1f}% |\n")

    tiers = subtests.get("tiers", {})
    if tiers:
        out.append("### 场景级(per-case,DCAT_SUBTEST)\n")
        out.append("| 层 | 总数 | 通过 | 失败 |")
        out.append("|---|---:|---:|---:|")
        gt = gp = gf = 0
        for tier in sorted(tiers):
            t = tiers[tier]
            out.append(f"| {tier} | {t['total']} | {t['pass']} | {t['fail']} |")
            gt += t["total"]; gp += t["pass"]; gf += t["fail"]
        out.append(f"| **合计** | **{gt}** | **{gp}** | **{gf}** |\n")

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
    log = sys.argv[2] if len(sys.argv) > 2 else LOG_DEFAULT
    arch = platform.machine() or "unknown"
    mode = os.environ.get("DCAT_TEST_MODE") or "regular"
    parsed = parse_junit(junit)
    subtests = parse_subtests(log)
    md = render_markdown(parsed, subtests, arch, mode)

    print(md)
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
