#!/bin/sh
# verify.sh — 用 dcat query 判定某个**可查询**故障是否真正生效，供 agent 取证。
#
# ⚠️ dcat query 的退出码 0 不代表故障生效（未注入时也返回 0）；生效只看 JSON data.confirmed。
# ⚠️ inject-only 故障无 query op（dcat 报 "op not in supported_ops"）→ 无法验证，返回 NOT_VERIFIABLE。
#
# 用法:  sh <本 SKILL.md 所在目录>/scripts/verify.sh <uid> [--<参数>=<值> ...]
#        （用 sh 调用 + 给 skill 目录下的路径：既不依赖可执行位，也不依赖当前目录）
# 退出码: 0=CONFIRMED  1=NOT_ACTIVE  2=用法错误  3=NOT_VERIFIABLE(inject-only)  4=NOT_FOUND  5=ERROR

set -u
DCAT="${DCAT_BIN:-dcat}"

uid="${1:-}"
if [ -z "$uid" ]; then
  echo "usage: sh verify.sh <uid> [--<参数>=<值> ...]" >&2
  exit 2
fi
shift

if ! command -v "$DCAT" >/dev/null 2>&1; then
  echo "ERROR $uid (dcat not found: $DCAT)"
  exit 5
fi

out=$("$DCAT" query "$uid" "$@" 2>&1)
rc=$?

# 仅在字符串精确匹配 inject-only 时才判不可验证（不靠 exit code，避免把参数错误等误判）
if printf '%s' "$out" | grep -Eq '"confirmed"[[:space:]]*:[[:space:]]*true'; then
  echo "CONFIRMED $uid"; printf '%s\n' "$out"; exit 0
fi
if printf '%s' "$out" | grep -q 'op not in supported_ops'; then
  echo "NOT_VERIFIABLE $uid (inject-only: no query op — 不可逆，勿谎报成功)"; printf '%s\n' "$out"; exit 3
fi
if [ "$rc" -eq 4 ] || printf '%s' "$out" | grep -q 'not found in catalog'; then
  echo "NOT_FOUND $uid (uid 不在目录；用 dcat list 核对)"; printf '%s\n' "$out"; exit 4
fi
if printf '%s' "$out" | grep -Eq '"confirmed"[[:space:]]*:[[:space:]]*false'; then
  echo "NOT_ACTIVE $uid"; printf '%s\n' "$out"; exit 1
fi
echo "ERROR $uid (dcat query failed, exit $rc)"; printf '%s\n' "$out"; exit 5
