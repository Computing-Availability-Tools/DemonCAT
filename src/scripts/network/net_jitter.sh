#!/bin/sh
# rNET_jitter: delay + jitter via tc netem (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_jitter-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        delay=${DCAT_PARAM_DELAY_MS:?missing required param: delay_ms}
        jitter=${DCAT_PARAM_JITTER_MS:?missing required param: jitter_ms}
        SIDECAR="/tmp/dcat-rNET_jitter-${iface}.sidecar"
        tc qdisc add dev "$iface" root netem delay "${delay}ms" "${jitter}ms" || { echo "tc add failed (need root?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${delay}ms +/- ${jitter}ms jitter on $iface"
        ;;
    clean)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "/tmp/dcat-rNET_jitter-${iface}.sidecar"
        echo "cleaned jitter on $iface"
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces=$DCAT_PARAM_IFACE
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_jitter-*.sidecar; do
                [ -f "$sc" ] || continue
                ifaces="$ifaces $(cat "$sc" 2>/dev/null)"
            done
        fi
        found=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            out=$(tc qdisc show dev "$iface" 2>/dev/null)
            match=$(echo "$out" | grep -E "netem.*delay [0-9.]+[a-z]*[[:space:]]+[0-9.]+[a-z]*")
            [ -n "$match" ] && { echo "$match"; found=1; }
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
