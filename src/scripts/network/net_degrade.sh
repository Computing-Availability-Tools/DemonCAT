#!/bin/sh
# rNET_degrade: NIC speed degrade via ethtool (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_degrade-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        speed=${DCAT_PARAM_SPEED_MBPS:-10}
        SIDECAR="/tmp/dcat-rNET_degrade-${iface}.sidecar"
        ethtool -s "$iface" speed "$speed" || { echo "ethtool -s failed (need root + driver support?)" >&2; exit 1; }
        echo "$iface $speed" > "$SIDECAR"
        echo "degraded $iface to ${speed}Mbps"
        ;;
    clean)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_degrade-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_degrade-}; v=${v%.sidecar}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            ethtool -s "$iface" speed 1000 autoneg on 2>/dev/null
            rm -f "/tmp/dcat-rNET_degrade-${iface}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "restored [$ifaces] speed";
        else echo "restored (no active injection)"; fi
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces=$DCAT_PARAM_IFACE
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_degrade-*.sidecar; do
                [ -f "$sc" ] || continue
                ifaces="$ifaces $(awk '{print $1}' "$sc" 2>/dev/null)"
            done
        fi
        found=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            # 期望速度: --speed_mbps 优先, 否则从 sidecar 读注入时速度
            expected=$DCAT_PARAM_SPEED_MBPS
            [ -n "$expected" ] || expected=$(awk '{print $2}' "/tmp/dcat-rNET_degrade-${iface}.sidecar" 2>/dev/null)
            [ -n "$expected" ] || continue
            speed=$(ethtool "$iface" 2>/dev/null | grep -E "Speed:" | grep -oE "[0-9]+")
            if [ -n "$speed" ] && [ "$speed" = "$expected" ]; then
                echo "$iface: speed=${speed}Mbps (expected ${expected})"
                found=1
            fi
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
