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
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_reorder-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_reorder-}; v=${v%.sidecar}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            tc qdisc del dev "$iface" root 2>/dev/null
            rm -f "/tmp/dcat-rNET_reorder-${iface}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "cleaned reorder on [$ifaces]";
        else echo "cleaned reorder (no active injection)"; fi
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces=$DCAT_PARAM_IFACE
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_reorder-*.sidecar; do
                [ -f "$sc" ] || continue
                ifaces="$ifaces $(cat "$sc" 2>/dev/null)"
            done
        fi
        found=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            out=$(tc qdisc show dev "$iface" 2>/dev/null)
            match=$(echo "$out" | grep -E "netem.*reorder")
            [ -n "$match" ] && { echo "$match"; found=1; }
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
