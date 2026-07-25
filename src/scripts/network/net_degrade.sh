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
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null | awk '{print $1}')}"
        [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
        ethtool -s "$iface" speed 1000 autoneg on 2>/dev/null
        rm -f "/tmp/dcat-rNET_degrade-${iface}.sidecar"
        echo "restored $iface speed"
        ;;
    query)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null | awk '{print $1}')}"
        expected=${DCAT_PARAM_SPEED_MBPS:-10}
        out=$(ethtool "$iface" 2>/dev/null)
        speed=$(echo "$out" | grep -E "Speed:" | grep -oE "[0-9]+")
        echo "$out"
        [ -n "$speed" ] && [ "$speed" = "$expected" ] && exit 0 || exit 1
        ;;
esac
