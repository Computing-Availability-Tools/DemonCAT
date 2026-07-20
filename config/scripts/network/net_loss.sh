#!/bin/sh
# rNET_loss: packet loss via tc netem.
# Env: DCAT_OP, DCAT_PARAM_IFACE, DCAT_PARAM_LOSS_PCT
SIDECAR=/run/demoncat/rNET_loss.iface
case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing iface}
        pct=${DCAT_PARAM_LOSS_PCT:?missing loss_pct}
        if ! tc qdisc add dev "$iface" root netem loss random "${pct}%" 2>&1; then
            echo "tc add failed (need root/CAP_NET_ADMIN?)" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$iface" > "$SIDECAR" 2>/dev/null
        echo "applied ${pct}% loss on $iface"
        ;;
    clean)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "$SIDECAR" 2>/dev/null
        echo "cleaned loss on $iface"
        ;;
    query)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        out=$(tc qdisc show dev "$iface" 2>/dev/null)
        if echo "$out" | grep -qE "netem.*loss"; then
            echo "FAULT CONFIRMED: netem loss active on $iface"
            echo "$out"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no netem loss on $iface"
            echo "$out"
            exit 1
        fi
        ;;
esac
