#!/usr/bin/env bash
# 剩余 20 条非手测故障的真实二进制测试
export HOME=/tmp/dcat-realtest3
mkdir -p "$HOME"
pkill -x perl 2>/dev/null; pkill -x dd 2>/dev/null
rm -rf "$HOME/.demoncat"
cd /mnt/d/CAT
DCAT=./build/dcat
ZPID=""; FLAP_PID=""
ip link set dummy0 up 2>/dev/null

cleanup() {
  echo; echo "=== cleanup ==="
  $DCAT clean rNET_loss      --iface=dummy0 2>/dev/null
  $DCAT clean rNET_reorder   --iface=dummy0 2>/dev/null
  $DCAT clean rNET_jitter    --iface=dummy0 2>/dev/null
  $DCAT clean rNET_bw_limit  --iface=dummy0 2>/dev/null
  $DCAT clean rNET_link_flap --iface=dummy0 2>/dev/null
  $DCAT clean rNET_tcp_loss  --port=19110   2>/dev/null
  $DCAT clean rPROC_zstate   --pid=$ZPID    2>/dev/null
  pkill -x perl 2>/dev/null; pkill -x dd 2>/dev/null
  iptables -D OUTPUT -p tcp --dport 19110 -j DROP 2>/dev/null
  tc qdisc del dev dummy0 root 2>/dev/null
  [ -n "$ZPID" ] && kill "$ZPID" 2>/dev/null
  rm -rf /tmp/dcat-realtest3
}
trap cleanup EXIT

cycle() {
  local uid="$1"; local label="$2"; shift 2
  echo "===== $label ====="
  echo "-- inject:        "; $DCAT inject $uid "$@"
  echo "-- re-inject(RJ): "; $DCAT inject $uid "$@"
  echo "-- --force:       "; $DCAT inject $uid "$@" --force
  echo "-- query:          "; $DCAT query
  echo "-- clean:          "; $DCAT clean $uid "$@"
  echo "-- query(empty):  "; $DCAT query
  echo
}

cycle rNET_loss      "NET loss (iface=dummy0, tc netem loss)"   --iface=dummy0 --loss_pct=5
cycle rNET_reorder   "NET reorder (iface=dummy0, tc netem)"     --iface=dummy0 --reorder_pct=10
cycle rNET_jitter    "NET jitter (iface=dummy0, tc netem)"     --iface=dummy0 --delay_ms=50 --jitter_ms=10
cycle rNET_bw_limit  "NET bw_limit (iface=dummy0, tc tbf)"     --iface=dummy0 --rate_kbps=1000
cycle rNET_link_flap "NET link_flap (iface=dummy0, ip link)"   --iface=dummy0
cycle rNET_tcp_loss  "NET tcp_loss (port=19110, iptables)"     --port=19110

echo "===== PROC zstate (pid, zombie) ====="
sleep 600 & ZPID=$!
echo "sacrificial pid=$ZPID"
echo "-- inject:        "; $DCAT inject rPROC_zstate --pid=$ZPID
echo "-- re-inject(RJ): "; $DCAT inject rPROC_zstate --pid=$ZPID
echo "-- --force:       "; $DCAT inject rPROC_zstate --pid=$ZPID --force
echo "-- query:          "; $DCAT query
echo "-- clean:          "; $DCAT clean rPROC_zstate --pid=$ZPID
echo "-- query(empty):  "; $DCAT query
echo

echo "===== 13 NPU (non-manual, no hccn_tool → graceful fail; no state → no reinject) ====="
# format: uid|params
NPU_LIST=(
  "rNPU_netdetect_change|--chip=0 --address=1.1.1.1"
  "rNPU_arp_poison|--chip=0 --dev=eth0 --ip=1.1.1.1 --mac=de:ad:01:02:03:04"
  "rNPU_arp_del|--chip=0 --dev=eth0 --ip=1.1.1.1"
  "rNPU_route_add|--chip=0 --address=1.1.1.1 --netmask=24 --gateway=1.1.1.254"
  "rNPU_route_del|--chip=0 --address=1.1.1.1 --netmask=24"
  "rNPU_iprule_add|--chip=0 --dir=in --ip=1.1.1.1 --table=100"
  "rNPU_iprule_del|--chip=0 --dir=in --ip=1.1.1.1"
  "rNPU_iproute_add|--chip=0 --ip=1.1.1.1 --ip_mask=24 --via=1.1.1.254 --dev=eth0 --table=100"
  "rNPU_iproute_del|--chip=0 --ip=1.1.1.1 --ip_mask=24 --table=100"
  "rNPU_bw_limit|--chip=0 --bw_limit=1000"
  "rNPU_dscp_tc_change|--chip=0 --dscp=46 --tc=1"
  "rNPU_prio_tc_change|--chip=0 --map=0:1"
  "rNPU_pfc_change|--chip=0 --bitmap=ff"
)
for entry in "${NPU_LIST[@]}"; do
  uid="${entry%%|*}"; params="${entry#*|}"
  echo "-- $uid inject: "; $DCAT inject $uid $params
  echo "   query(empty):"; $DCAT query
  echo "   clean:       "; $DCAT clean $uid $params
  echo
done
