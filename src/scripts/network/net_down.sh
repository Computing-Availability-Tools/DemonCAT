#!/bin/sh
# rNET_down: NIC down via ip link (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_down-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        # Validate iface: alphanumeric, underscore, hyphen only
        case "$iface" in ''|*[!a-zA-Z0-9_-]*) echo "invalid iface: '$iface'" >&2; exit 1 ;; esac
        # 管理网卡防护：拒绝 down 承载活跃默认路由的接口（避免远程连接丢失）。
        # 用 `ip route get` 定位实际生效的出接口；`ip route show default` 会列出
        # 多条（含 linkdown 的备路由 metric 101），把非活跃备卡也误拦了。
        if ip route get 8.8.8.8 2>/dev/null | grep -q "dev $iface"; then
            echo "refused: $iface carries the active default route (management NIC), refusing to bring it down" >&2
            exit 1
        fi
        SIDECAR="/tmp/dcat-rNET_down-${iface}.sidecar"
        ip link set dev "$iface" down || { echo "ip link set down failed (need root?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "brought $iface down"
        ;;
    clean)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_down-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_down-}; v=${v%.sidecar}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            ip link set dev "$iface" up 2>/dev/null
            rm -f "/tmp/dcat-rNET_down-${iface}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "brought [$ifaces] up";
        else echo "brought up (no active injection)"; fi
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces=$DCAT_PARAM_IFACE
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_down-*.sidecar; do
                [ -f "$sc" ] || continue
                ifaces="$ifaces $(cat "$sc" 2>/dev/null)"
            done
        fi
        found=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            out=$(ip -o link show dev "$iface" 2>/dev/null)
            echo "$out"
            echo "$out" | grep -q "state DOWN" && found=1
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
