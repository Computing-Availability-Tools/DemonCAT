#!/bin/sh
# rNET_reorder: packet reorder via tc netem (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_reorder-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        pct=${DCAT_PARAM_REORDER_PCT:?missing required param: reorder_pct}
        SIDECAR="/tmp/dcat-rNET_reorder-${iface}.sidecar"
        tc qdisc add dev "$iface" root netem delay 10ms reorder "${pct}%" 50% || { echo "tc add failed (need root?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${pct}% reorder on $iface"
        ;;
    clean)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "/tmp/dcat-rNET_reorder-${iface}.sidecar"
        echo "cleaned reorder on $iface"
        ;;
    query)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        out=$(tc qdisc show dev "$iface" 2>/dev/null)
        echo "$out"
        echo "$out" | grep -qE "netem.*reorder" && exit 0 || exit 1
        ;;
esac
