#!/bin/sh
# rNET_down: NIC down via ip link.
# Env: DCAT_OP, DCAT_PARAM_IFACE
SIDECAR=/run/demoncat/rNET_down.iface
case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing iface}
        if ! ip link set dev "$iface" down 2>&1; then
            echo "ip link set down failed (need root?)" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$iface" > "$SIDECAR" 2>/dev/null
        echo "brought $iface down"
        ;;
    clean)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        ip link set dev "$iface" up 2>/dev/null
        rm -f "$SIDECAR" 2>/dev/null
        echo "brought $iface up"
        ;;
    query)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        out=$(ip -o link show dev "$iface" 2>/dev/null)
        if echo "$out" | grep -q "state DOWN"; then
            echo "FAULT CONFIRMED: $iface is DOWN"
            echo "$out"
            exit 0
        else
            echo "FAULT NOT ACTIVE: $iface is not DOWN"
            echo "$out"
            exit 1
        fi
        ;;
esac
