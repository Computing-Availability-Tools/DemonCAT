#!/usr/bin/env bash
# 真实二进制端到端测试: inject/query/clean/reject/--force 回显
export HOME=/tmp/dcat-realtest
mkdir -p "$HOME"
pkill -x perl >/dev/null 2>&1
rm -rf "$HOME/.demoncat"      # 全新环境: 不预建, 验证 dcat 自建父目录
cd /mnt/d/CAT
DCAT=./build/dcat

cleanup() {
  echo
  echo "=== cleanup ==="
  $DCAT clean rCPU_overload --cores=0,1   >/dev/null 2>&1
  $DCAT clean rCPU_overload --cores=0-1   >/dev/null 2>&1
  $DCAT clean rCPU_overload --cores=0,1,2 >/dev/null 2>&1
  pkill -x perl >/dev/null 2>&1
  rm -rf /tmp/dcat-realtest
}
trap cleanup EXIT

echo "=== 1. query baseline (expect empty) ==="
$DCAT query
echo

echo "=== 2. inject rCPU_overload --cores=0,1 (expect ok + record_id) ==="
$DCAT inject rCPU_overload --cores=0,1
echo

echo "=== 3. re-inject SAME (expect REJECT code 5 + message shows 'cores=0,1') ==="
$DCAT inject rCPU_overload --cores=0,1
echo

echo "=== 4. re-inject OVERLAP cores=0,1,2 (expect REJECT + message 'cores=0,1') ==="
$DCAT inject rCPU_overload --cores=0,1,2
echo

echo "=== 5. re-inject OVERLAP cores=0-1 set semantics (expect REJECT + 'cores=0,1') ==="
$DCAT inject rCPU_overload --cores=0-1
echo

echo "=== 6. query (expect 1 active record, cores=0,1) ==="
$DCAT query
echo

echo "=== 7. --force replace (clean old 0,1 + inject new 0,1) ==="
$DCAT inject rCPU_overload --cores=0,1 --force
echo

echo "=== 8. perl count (expect 2, NOT 4 -> force did not double) ==="
pgrep -x perl | wc -l
echo

echo "=== 9. clean rCPU_overload --cores=0,1 (expect ok) ==="
$DCAT clean rCPU_overload --cores=0,1
echo

echo "=== 10. query (expect empty) ==="
$DCAT query
echo

echo "=== 11. perl count (expect 0) ==="
pgrep -x perl | wc -l
