#!/bin/sh
# rNET_bw_limit: bandwidth limit via tc tbf (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_bw_limit-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        # Validate iface: alphanumeric, underscore, hyphen only
        case "$iface" in ''|*[!a-zA-Z0-9_-]*) echo "invalid iface: '$iface'" >&2; exit 1 ;; esac
        rate=${DCAT_PARAM_RATE_KBPS:?missing required param: rate_kbps}
        case "$rate" in ''|*[!0-9]*) echo "rate_kbps must be a positive integer, got: '$rate'" >&2; exit 1 ;; esac
        SIDECAR="/tmp/dcat-rNET_bw_limit-${iface}.sidecar"
        tc qdisc add dev "$iface" root tbf rate "${rate}kbit" burst 32kbit latency 400ms || { echo "$iface 已有 root qdisc ($(tc qdisc show dev "$iface" 2>/dev/null | grep root | head -1))" >&2; echo "请先清理: dcat clean --all 或 tc qdisc del dev $iface root" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${rate}kbps limit on $iface"
        ;;
    clean)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_bw_limit-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_bw_limit-}; v=${v%.sidecar}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            tc qdisc del dev "$iface" root 2>/dev/null
            rm -f "/tmp/dcat-rNET_bw_limit-${iface}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "cleaned bw limit on [$ifaces]";
        else echo "cleaned bw limit (no active injection)"; fi
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
            match=$(echo "$out" | grep -E "qdisc tbf")
            [ -n "$match" ] && { echo "[$iface] $match"; found=1; }
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
