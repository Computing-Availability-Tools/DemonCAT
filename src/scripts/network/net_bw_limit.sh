#!/bin/sh
# rNET_bw_limit: bandwidth limit via tc tbf (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_bw_limit-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        rate=${DCAT_PARAM_RATE_KBPS:?missing required param: rate_kbps}
        SIDECAR="/tmp/dcat-rNET_bw_limit-${iface}.sidecar"
        tc qdisc add dev "$iface" root tbf rate "${rate}kbit" burst 32kbit latency 400ms || { echo "tc add failed (need root?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${rate}kbps limit on $iface"
        ;;
    clean)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "/tmp/dcat-rNET_bw_limit-${iface}.sidecar"
        echo "cleaned bw limit on $iface"
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces=$DCAT_PARAM_IFACE
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_bw_limit-*.sidecar; do
                [ -f "$sc" ] || continue
                ifaces="$ifaces $(cat "$sc" 2>/dev/null)"
            done
        fi
        found=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            out=$(tc qdisc show dev "$iface" 2>/dev/null)
            echo "$out"
            echo "$out" | grep -qE "qdisc tbf" && found=1
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
