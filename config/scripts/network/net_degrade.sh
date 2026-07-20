#!/bin/sh
# rNET_degrade: NIC speed degrade via ethtool.
# Env: DCAT_OP, DCAT_PARAM_IFACE, DCAT_PARAM_SPEED_MBPS (default 10)
SIDECAR=/run/demoncat/rNET_degrade.iface
case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing iface}
        speed=${DCAT_PARAM_SPEED_MBPS:-10}
        if ! ethtool -s "$iface" speed "$speed" 2>&1; then
            echo "ethtool -s failed (need root + driver support?)" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$iface" > "$SIDECAR" 2>/dev/null
        echo "degraded $iface to ${speed}Mbps"
        ;;
    clean)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        ethtool -s "$iface" speed 1000 autoneg on 2>/dev/null
        rm -f "$SIDECAR" 2>/dev/null
        echo "restored $iface speed"
        ;;
    query)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        expected=${DCAT_PARAM_SPEED_MBPS:-10}
        out=$(ethtool "$iface" 2>/dev/null)
        speed=$(echo "$out" | grep -E "Speed:" | grep -oE "[0-9]+")
        if [ -n "$speed" ] && [ "$speed" = "$expected" ]; then
            echo "FAULT CONFIRMED: $iface degraded to ${speed}Mbps"
            echo "$out"
            exit 0
        else
            echo "FAULT NOT ACTIVE: $iface at ${speed:-unknown}Mbps (expected ${expected}Mbps)"
            exit 1
        fi
        ;;
esac
