#!/bin/sh
# rNET_loss: packet loss via tc netem (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_loss-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        pct=${DCAT_PARAM_LOSS_PCT:?missing required param: loss_pct}
        SIDECAR="/tmp/dcat-rNET_loss-${iface}.sidecar"
        tc qdisc add dev "$iface" root netem loss random "${pct}%" || { echo "tc add failed (need root?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${pct}% loss on $iface"
        ;;
    clean)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "/tmp/dcat-rNET_loss-${iface}.sidecar"
        echo "cleaned loss on $iface"
        ;;
    query)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        out=$(tc qdisc show dev "$iface" 2>/dev/null)
        echo "$out"
        echo "$out" | grep -qE "netem.*loss" && exit 0 || exit 1
        ;;
esac
