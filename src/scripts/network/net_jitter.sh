#!/bin/sh
# rNET_jitter: delay + jitter via tc netem (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_jitter-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        # Validate iface: alphanumeric, underscore, hyphen only
        case "$iface" in ''|*[!a-zA-Z0-9_-]*) echo "invalid iface: '$iface'" >&2; exit 1 ;; esac
        delay=${DCAT_PARAM_DELAY_MS:?missing required param: delay_ms}
        jitter=${DCAT_PARAM_JITTER_MS:?missing required param: jitter_ms}
        case "$delay" in ''|*[!0-9]*) echo "delay_ms must be a positive integer, got: '$delay'" >&2; exit 1 ;; esac
        case "$jitter" in ''|*[!0-9]*) echo "jitter_ms must be a positive integer, got: '$jitter'" >&2; exit 1 ;; esac
        SIDECAR="/tmp/dcat-rNET_jitter-${iface}.sidecar"
        tc qdisc add dev "$iface" root netem delay "${delay}ms" "${jitter}ms" || { echo "$iface 已有 root qdisc ($(tc qdisc show dev "$iface" 2>/dev/null | grep root | head -1))" >&2; echo "请先清理: dcat clean --all 或 tc qdisc del dev $iface root" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${delay}ms +/- ${jitter}ms jitter on $iface"
        ;;
    clean)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_jitter-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_jitter-}; v=${v%.sidecar}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            tc qdisc del dev "$iface" root 2>/dev/null
            rm -f "/tmp/dcat-rNET_jitter-${iface}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "cleaned jitter on [$ifaces]";
        else echo "cleaned jitter (no active injection)"; fi
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
            [ -n "$match" ] && { echo "[$iface] $match"; found=1; }
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
