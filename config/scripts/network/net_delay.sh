#!/bin/sh
# rNET_delay: network egress delay via tc netem (sync mode).
# Env: DCAT_OP, DCAT_PARAM_IFACE, DCAT_PARAM_DELAY_MS
# Requires root (CAP_NET_ADMIN). Writes a sidecar so param-less clean can undo.
SIDECAR=/run/demoncat/rNET_delay.iface
case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing iface}
        delay=${DCAT_PARAM_DELAY_MS:?missing delay_ms}
        if ! tc qdisc add dev "$iface" root netem delay "${delay}ms" 2>&1; then
            echo "tc add failed (need root/CAP_NET_ADMIN?)" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$iface" > "$SIDECAR" 2>/dev/null
        echo "applied ${delay}ms delay on $iface"
        ;;
    clean)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "$SIDECAR" 2>/dev/null
        echo "cleaned delay on $iface"
        ;;
    query)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        out=$(tc qdisc show dev "$iface" 2>/dev/null)
        if echo "$out" | grep -qE "netem.*delay [0-9]+[a-z]*$"; then
            echo "FAULT CONFIRMED: netem delay active on $iface"
            echo "$out"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no netem delay on $iface"
            echo "$out"
            exit 1
        fi
        ;;
esac
