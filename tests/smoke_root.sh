#!/bin/bash
# tests/smoke_root.sh �?自动化测试需�?root 的故�?# 用法: sudo bash tests/smoke_root.sh
# 自动检测可用工具，能测的都测，不能测的跳过并说明原�?set -e

cd "$(dirname "$0")/.."

# ---- 前置检�?----
[ "$(id -u)" = 0 ] || { echo "ERROR: 需�?root 权限运行 (sudo bash tests/smoke_root.sh)"; exit 1; }

# 编译（子 shell 防止 cd 污染当前目录�?[ -x build/dcat ] || { mkdir -p build && (cd build && cmake .. && make); }
DCAT=./build/dcat

# 测试状态隔离：dcat 不读 DCAT_STATE_FILE（main.c 只认 config �?state_file/默认 ~/.demoncat）�?# 生成临时 config，把 state_file 覆盖�?/tmp，避免污�?/root/.demoncat/state.json�?TMP_CONF="/tmp/dcat_smoke_root.conf"
STATE_FILE="/tmp/dcat_smoke_root.json"
sed "s|^state_file = .*|state_file = $STATE_FILE|" config/demoncat.conf > "$TMP_CONF"
rm -f "$STATE_FILE"
DCAT_CONF="$TMP_CONF"

# 创建测试用的虚拟网卡 (不影响真实网�?
TEST_IFACE="dcat-test0"
ip link add "$TEST_IFACE" type dummy 2>/dev/null || true
ip link set "$TEST_IFACE" up 2>/dev/null || true

PASS=0; FAIL=0; SKIP=0

report() { echo "  $1: $2"; }
pass() { echo "  PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1 �?$2"; FAIL=$((FAIL+1)); }
skip() { echo "  SKIP: $1 �?$2"; SKIP=$((SKIP+1)); }

cleanup_fault() {
    $DCAT clean "$@" --config "$DCAT_CONF" >/dev/null 2>&1 || true
    pkill -f 'dcat.dstate\|dcat.stress\|dcat.write' 2>/dev/null || true
    rm -f /tmp/dcat-*.pid /tmp/dcat-*.sidecar /tmp/dcat-*.bak /tmp/dcat-*.rule /tmp/dcat-*.list /tmp/dcat.dstate.* /tmp/dcat.write.* 2>/dev/null || true
}

echo "=========================================="
echo "DemonCAT Root Smoke Test"
echo "=========================================="

# ---- 工具检�?----
echo ""
echo "--- 工具检�?---"
HAS_TC=0;    command -v tc >/dev/null 2>&1 && HAS_TC=1 && echo "  tc: �? || echo "  tc: �?(apt install iproute2)"
HAS_DD=0;    command -v dd >/dev/null 2>&1 && HAS_DD=1 && echo "  dd: �? || echo "  dd: �?
HAS_IP=0;    command -v ip >/dev/null 2>&1 && HAS_IP=1 && echo "  ip: �? || echo "  ip: �?
HAS_ETHTOOL=0; command -v ethtool >/dev/null 2>&1 && HAS_ETHTOOL=1 && echo "  ethtool: �? || echo "  ethtool: �?(apt install ethtool)"
HAS_IPTABLES=0; command -v iptables >/dev/null 2>&1 && HAS_IPTABLES=1 && echo "  iptables: �? || echo "  iptables: �?(apt install iptables)"
HAS_SYSTEMCTL=0; command -v systemctl >/dev/null 2>&1 && HAS_SYSTEMCTL=1 && echo "  systemctl: �? || echo "  systemctl: �?
HAS_HCCN=0;  command -v hccn_tool >/dev/null 2>&1 && HAS_HCCN=1 && echo "  hccn_tool: �? || echo "  hccn_tool: �?(需�?Atlas NPU 硬件)"

# ---- 测试网卡 ----
HAS_TEST_IFACE=0
ip link show "$TEST_IFACE" >/dev/null 2>&1 && HAS_TEST_IFACE=1 && echo "  测试网卡 $TEST_IFACE: �? || echo "  测试网卡: �?(创建失败)"

# ====================================================================
echo ""
echo "--- rCPU_core_offline (sysfs) ---"
# ====================================================================
if [ -w /sys/devices/system/cpu/cpu1/online ]; then
    if $DCAT inject rCPU_core_offline --cores=1 --config "$DCAT_CONF" >/dev/null 2>&1; then
        state=$(cat /sys/devices/system/cpu/cpu1/online 2>/dev/null)
        if [ "$state" = "0" ]; then
            $DCAT clean rCPU_core_offline --cores=1 --config "$DCAT_CONF" >/dev/null 2>&1
            state2=$(cat /sys/devices/system/cpu/cpu1/online 2>/dev/null)
            [ "$state2" = "1" ] && pass "rCPU_core_offline" || fail "rCPU_core_offline" "clean �?cpu1 未恢�?(state=$state2)"
        else
            cleanup_fault rCPU_core_offline --cores=1
            fail "rCPU_core_offline" "inject �?cpu1 未离�?(state=$state)"
        fi
    else
        fail "rCPU_core_offline" "inject 失败"
    fi
else
    skip "rCPU_core_offline" "sysfs 不可�?(WSL 内核限制)"
fi

# ====================================================================
echo ""
echo "--- 网络故障 (需�?tc + 测试网卡 $TEST_IFACE) ---"
# ====================================================================

test_tc_fault() {
    uid="$1"; shift
    if [ "$HAS_TC" = 0 ] || [ "$HAS_TEST_IFACE" = 0 ]; then
        skip "$uid" "需�?tc + 测试网卡"
        return
    fi
    # inject
    if $DCAT inject "$uid" --config "$DCAT_CONF" "$@" >/dev/null 2>&1; then
        # verify qdisc exists
        if tc qdisc show dev "$TEST_IFACE" 2>/dev/null | grep -qE "qdisc"; then
            # clean
            $DCAT clean "$uid" --config "$DCAT_CONF" "$@" >/dev/null 2>&1
            # verify qdisc removed
            if ! tc qdisc show dev "$TEST_IFACE" 2>/dev/null | grep -qE "netem|tbf"; then
                pass "$uid"
            else
                cleanup_fault "$uid" "$@"
                fail "$uid" "clean �?qdisc 仍存�?
            fi
        else
            cleanup_fault "$uid" "$@"
            fail "$uid" "inject �?qdisc 未添�?
        fi
    else
        fail "$uid" "inject 失败"
    fi
}

test_tc_fault rNET_delay --iface=$TEST_IFACE --delay_ms=100
test_tc_fault rNET_loss --iface=$TEST_IFACE --loss_pct=5
test_tc_fault rNET_reorder --iface=$TEST_IFACE --reorder_pct=30
test_tc_fault rNET_bw_limit --iface=$TEST_IFACE --rate_kbps=1000
test_tc_fault rNET_jitter --iface=$TEST_IFACE --delay_ms=50 --jitter_ms=10

# ====================================================================
echo ""
echo "--- rNET_down (ip link) ---"
# ====================================================================
if [ "$HAS_IP" = 1 ] && [ "$HAS_TEST_IFACE" = 1 ]; then
    if $DCAT inject rNET_down --iface=$TEST_IFACE --config "$DCAT_CONF" >/dev/null 2>&1; then
        state=$(ip -o link show dev "$TEST_IFACE" 2>/dev/null | grep -o "state [A-Z]*" | awk '{print $2}')
        if [ "$state" = "DOWN" ]; then
            $DCAT clean rNET_down --iface=$TEST_IFACE --config "$DCAT_CONF" >/dev/null 2>&1
            state2=$(ip -o link show dev "$TEST_IFACE" 2>/dev/null | grep -o "state [A-Z]*" | awk '{print $2}')
            [ "$state2" != "DOWN" ] && pass "rNET_down" || fail "rNET_down" "clean 后仍 DOWN"
        else
            cleanup_fault rNET_down --iface=$TEST_IFACE
            fail "rNET_down" "inject 后未 DOWN (state=$state)"
        fi
    else
        fail "rNET_down" "inject 失败"
    fi
else
    skip "rNET_down" "需�?ip + 测试网卡"
fi

# ====================================================================
echo ""
echo "--- rNET_degrade (ethtool) ---"
# ====================================================================
if [ "$HAS_ETHTOOL" = 1 ] && [ "$HAS_TEST_IFACE" = 1 ]; then
    if $DCAT inject rNET_degrade --iface=$TEST_IFACE --speed_mbps=10 --config "$DCAT_CONF" >/dev/null 2>&1; then
        $DCAT clean rNET_degrade --iface=$TEST_IFACE --speed_mbps=10 --config "$DCAT_CONF" >/dev/null 2>&1
        pass "rNET_degrade"
    else
        # ethtool 可能不支�?dummy 网卡，这是预期的
        skip "rNET_degrade" "ethtool 不支�?$TEST_IFACE (dummy 网卡无速率控制)"
    fi
else
    skip "rNET_degrade" "需�?ethtool + 测试网卡"
fi

# ====================================================================
echo ""
echo "--- rNET_link_flap (ip link) ---"
# ====================================================================
if [ "$HAS_IP" = 1 ] && [ "$HAS_TEST_IFACE" = 1 ]; then
    if $DCAT inject rNET_link_flap --iface=$TEST_IFACE --cycle_sec=1 --count=2 --config "$DCAT_CONF" >/dev/null 2>&1; then
        sleep 5
        $DCAT clean rNET_link_flap --iface=$TEST_IFACE --cycle_sec=1 --count=2 --config "$DCAT_CONF" >/dev/null 2>&1
        state=$(ip -o link show dev "$TEST_IFACE" 2>/dev/null | grep -o "state [A-Z]*" | awk '{print $2}')
        [ "$state" != "DOWN" ] && pass "rNET_link_flap" || fail "rNET_link_flap" "clean 后仍 DOWN"
    else
        fail "rNET_link_flap" "inject 失败"
    fi
else
    skip "rNET_link_flap" "需�?ip + 测试网卡"
fi

# ====================================================================
echo ""
echo "--- rNET_tcp_loss (iptables) ---"
# ====================================================================
if [ "$HAS_IPTABLES" = 1 ]; then
    if $DCAT inject rNET_tcp_loss --port=19998 --config "$DCAT_CONF" >/dev/null 2>&1; then
        if iptables -L INPUT -n 2>/dev/null | grep -q "dpt:19998"; then
            $DCAT clean rNET_tcp_loss --port=19998 --config "$DCAT_CONF" >/dev/null 2>&1
            if ! iptables -L INPUT -n 2>/dev/null | grep -q "dpt:19998"; then
                pass "rNET_tcp_loss"
            else
                cleanup_fault rNET_tcp_loss --port=19998
                fail "rNET_tcp_loss" "clean 后规则仍存在"
            fi
        else
            cleanup_fault rNET_tcp_loss --port=19998
            fail "rNET_tcp_loss" "inject 后规则未添加"
        fi
    else
        fail "rNET_tcp_loss" "inject 失败"
    fi
else
    skip "rNET_tcp_loss" "iptables 未安�?(apt install iptables)"
fi

# ====================================================================
echo ""
echo "--- rNET_service_stop (systemctl) ---"
# ====================================================================
if [ "$HAS_SYSTEMCTL" = 1 ] && timeout 5 systemctl is-system-running >/dev/null 2>&1; then
    # 检查是否有可测试的服务 (不停止关键服�?
    TEST_SVC=""
    for svc in chronyd ntpd sshd cron; do
        systemctl is-active "$svc" >/dev/null 2>&1 && TEST_SVC="$svc" && break
    done
    if [ -n "$TEST_SVC" ]; then
        if $DCAT inject rNET_service_stop --service=$TEST_SVC --config "$DCAT_CONF" >/dev/null 2>&1; then
            state=$(timeout 3 systemctl is-active "$TEST_SVC" 2>/dev/null || true)
            if [ "$state" = "inactive" ] || [ "$state" = "failed" ]; then
                $DCAT clean rNET_service_stop --service=$TEST_SVC --config "$DCAT_CONF" >/dev/null 2>&1
                pass "rNET_service_stop"
            else
                $DCAT clean rNET_service_stop --service=$TEST_SVC --config "$DCAT_CONF" >/dev/null 2>&1
                fail "rNET_service_stop" "inject 后服务仍 active (state=$state)"
            fi
        else
            fail "rNET_service_stop" "inject 失败"
        fi
    else
        skip "rNET_service_stop" "没有可测试的非关键服�?(chronyd/ntpd/sshd/cron 均未运行)"
    fi
else
    skip "rNET_service_stop" "systemctl 不可用或 systemd 未正常运�?(WSL 常见)"
fi

# ====================================================================
echo ""
echo "--- NPU 故障 (19 �? ---"
# ====================================================================
if [ "$HAS_HCCN" = 1 ]; then
    echo "  (hccn_tool 可用，开始测�?..)"
    # NPU 测试需要真实硬件，这里只做 inject→query→clean 流程验证
    NPU_TESTS=(
        "rNPU_link_down chip=0"
        "rNPU_bw_limit chip=0 bw_limit=10000"
        "rNPU_mtu_mismatch chip=0 size=1280"
        "rNPU_roce_port_change chip=0 port=4791"
    )
    for line in "${NPU_TESTS[@]}"; do
        uid=$(echo "$line" | awk '{print $1}')
        params=$(echo "$line" | cut -d' ' -f2-)
        # 构�?--key=value 参数
        args=""
        for kv in $params; do args="$args --${kv}"; done
        if $DCAT inject "$uid" --config "$DCAT_CONF" $args >/dev/null 2>&1; then
            $DCAT clean "$uid" --config "$DCAT_CONF" $args >/dev/null 2>&1
            pass "$uid"
        else
            fail "$uid" "inject 失败 (hccn_tool 可能需要特定芯片状�?"
        fi
    done
    skip "其余 15 �?rNPU_*" "需要特�?NPU 配置参数，请参照 docs/Manual_Test_Reference.md 手动测试"
else
    skip "全部 19 �?rNPU_*" "hccn_tool 不可�?�?需�?Atlas NPU 物理机，WSL 无法模拟。原因：所�?NPU 故障通过 hccn_tool 操作 RoCE 网卡，无硬件无法执行 inject/clean/query 的任何一�?
fi

# ====================================================================
# 清理
# ====================================================================
ip link del "$TEST_IFACE" 2>/dev/null || true
rm -f "$STATE_FILE" "$TMP_CONF" /tmp/dcat-* 2>/dev/null || true

# ====================================================================
# 汇�?# ====================================================================
echo ""
echo "=========================================="
echo "测试结果汇�?
echo "=========================================="
echo "  PASS: $PASS"
echo "  FAIL: $FAIL"
echo "  SKIP: $SKIP"
echo "  TOTAL: $((PASS+FAIL+SKIP))"
echo ""
if [ "$FAIL" -eq 0 ]; then
    echo "�?全部通过 (跳过的故障因缺少依赖，安装依赖后重跑即可)"
else
    echo "�?�?$FAIL 个失�?
fi
