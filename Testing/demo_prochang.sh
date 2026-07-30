#!/usr/bin/env bash
# rPROC_hang 正确用法演示: 后台非终端进程 (sleep) 可完美 STOP/CONT
export HOME=/tmp/dcat-hang-demo
mkdir -p "$HOME"; rm -rf "$HOME/.demoncat"
cd /mnt/d/CAT
DCAT=./build/dcat

cleanup() { $DCAT clean rPROC_hang --pid=$PID 2>/dev/null; kill $PID 2>/dev/null; rm -rf /tmp/dcat-hang-demo; }
trap cleanup EXIT

sleep 600 & PID=$!
echo "=== sacrificial background sleep pid=$PID (no controlling terminal) ==="
echo
echo "=== 1. inject (SIGSTOP) ==="
$DCAT inject rPROC_hang --pid=$PID
echo
echo "=== 2. /proc state (expect T = stopped) ==="
grep '^State:' /proc/$PID/status
echo
echo "=== 3. dcat query (expect confirmed:true, state T) ==="
$DCAT query rPROC_hang --pid=$PID; echo "(exit=$?)"
echo
echo "=== 4. clean (SIGCONT) ==="
$DCAT clean rPROC_hang --pid=$PID
echo
echo "=== 5. /proc state after clean (expect S = sleeping/resumed, NOT T) ==="
grep '^State:' /proc/$PID/status
echo
echo "=== 6. dcat query (expect confirmed:false — resumed, not stopped) ==="
$DCAT query rPROC_hang --pid=$PID; echo "(exit=$?)"
echo
echo "=== 7. prove sleep is alive + runnable ==="
kill -0 $PID && echo "pid $PID still alive"
