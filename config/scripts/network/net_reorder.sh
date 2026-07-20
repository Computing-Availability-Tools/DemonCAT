#!/bin/sh
# rNET_reorder: packet reorder via tc netem.
# Env: DCAT_OP, DCAT_PARAM_IFACE, DCAT_PARAM_REORDER_PCT
SIDECAR=/run/demoncat/rNET_reorder.iface
case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing iface}
        pct=${DCAT_PARAM_REORDER_PCT:?missing reorder_pct}
        if ! tc qdisc add dev "$iface" root netem delay 10ms reorder "${pct}%" 50% 2>&1; then
            echo "tc add failed (need root/CAP_NET_ADMIN?)" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$iface" > "$SIDECAR" 2>/dev/null
        echo "applied ${pct}% reorder on $iface"
        ;;
    clean)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "$SIDECAR" 2>/dev/null
        echo "cleaned reorder on $iface"
        ;;
    query)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        out=$(tc qdisc show dev "$iface" 2>/dev/null)
        if echo "$out" | grep -qE "netem.*reorder"; then
            echo "FAULT CONFIRMED: netem reorder active on $iface"
            echo "$out"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no netem reorder on $iface"
            echo "$out"
            exit 1
        fi
        ;;
esac
