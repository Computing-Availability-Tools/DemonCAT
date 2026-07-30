#!/usr/bin/env bash
# 跨模块真实二进制测试: 各故障 inject/query/clean + reinject 契约
export HOME=/tmp/dcat-realtest2
mkdir -p "$HOME"
pkill -x perl 2>/dev/null; pkill -x dd 2>/dev/null
rm -rf "$HOME/.demoncat"
cd /mnt/d/CAT
DCAT=./build/dcat
HANG_PID=""

cleanup() {
  echo; echo "=== cleanup ==="
  $DCAT clean rCPU_overload        --cores=0,1                 2>/dev/null
  $DCAT clean rCPU_core_offline    --cores=1                   2>/dev/null
  $DCAT clean rDISK_write_overload --device=/tmp/dcat-disk-test 2>/dev/null
  $DCAT clean rNET_port_occupy     --port=19100                2>/dev/null
  $DCAT clean rNET_port_occupy     --port=19101                2>/dev/null
  $DCAT clean rNET_delay           --iface=eth0               2>/dev/null
  $DCAT clean rPROC_hang           --pid=$HANG_PID            2>/dev/null
  pkill -x perl 2>/dev/null; pkill -x dd 2>/dev/null
  [ -n "$HANG_PID" ] && kill "$HANG_PID" 2>/dev/null
  rm -rf /tmp/dcat-realtest2 /tmp/dcat-disk-test
}
trap cleanup EXIT

# cycle uid "label" --params...  : inject → re-inject(REJECT) → --force → query → clean → query
cycle() {
  local uid="$1"; local label="$2"; shift 2
  echo "===== $label ====="
  echo "-- inject:        "; $DCAT inject $uid "$@"
  echo "-- re-inject(REJECT):"; $DCAT inject $uid "$@"
  echo "-- --force(replace):"; $DCAT inject $uid "$@" --force
  echo "-- query:          "; $DCAT query
  echo "-- clean:          "; $DCAT clean $uid "$@"
  echo "-- query(empty):  "; $DCAT query
  echo
}

cycle rCPU_overload        "1. CPU overload (cores=set resource key)" --cores=0,1
cycle rCPU_core_offline    "2. CPU core_offline (cores, sysfs)"      --cores=1
cycle rDISK_write_overload "3. DISK write_overload (device=scalar)"  --device=/tmp/dcat-disk-test --workers=2 --size_mb=5

echo "===== 4. NET port_occupy (port=scalar + concurrent-different) ====="
echo "-- inject port=19100:        "; $DCAT inject rNET_port_occupy --port=19100
echo "-- re-inject port=19100(REJECT):"; $DCAT inject rNET_port_occupy --port=19100
echo "-- inject port=19101(diff, OK):"; $DCAT inject rNET_port_occupy --port=19101
echo "-- query (expect 2 records):  "; $DCAT query
echo "-- --force port=19100(replace):"; $DCAT inject rNET_port_occupy --port=19100 --force
echo "-- clean both:                "; $DCAT clean rNET_port_occupy --port=19100; $DCAT clean rNET_port_occupy --port=19101
echo "-- query(empty):              "; $DCAT query
echo

echo "===== 5. NET delay (iface=scalar, tc netem — WSL may lack tc) ====="
echo "-- inject iface=eth0 delay=50: "; $DCAT inject rNET_delay --iface=eth0 --delay_ms=50
echo "-- re-inject(REJECT or tc-fail):"; $DCAT inject rNET_delay --iface=eth0 --delay_ms=50
echo "-- --force:                    "; $DCAT inject rNET_delay --iface=eth0 --delay_ms=50 --force
echo "-- clean:                      "; $DCAT clean rNET_delay --iface=eth0
echo

echo "===== 6. PROC hang (pid=scalar) ====="
sleep 600 & HANG_PID=$!
echo "sacrificial sleep pid=$HANG_PID"
echo "-- inject:        "; $DCAT inject rPROC_hang --pid=$HANG_PID
echo "-- re-inject(REJECT):"; $DCAT inject rPROC_hang --pid=$HANG_PID
echo "-- --force(replace):"; $DCAT inject rPROC_hang --pid=$HANG_PID --force
echo "-- query:          "; $DCAT query
echo "-- clean:          "; $DCAT clean rPROC_hang --pid=$HANG_PID
echo "-- query(empty):  "; $DCAT query
echo

echo "===== 7. PROC exit (inject-only → no state → exempt from reinject) ====="
sleep 600 & EXIT_PID=$!
echo "sacrificial pid=$EXIT_PID"
echo "-- inject (ok, kills it):   "; $DCAT inject rPROC_exit --pid=$EXIT_PID
echo "-- query (expect empty):    "; $DCAT query
echo "-- re-inject same pid (NOT code5 — exempt; pid dead):"; $DCAT inject rPROC_exit --pid=$EXIT_PID
echo

echo "===== 8. NPU link_down (chip — no hccn_tool/hardware, graceful fail) ====="
echo "-- inject (expect graceful fail):"; $DCAT inject rNPU_link_down --chip=0
echo "-- query (expect empty):        "; $DCAT query
echo "-- clean (no active injection):  "; $DCAT clean rNPU_link_down --chip=0
echo
