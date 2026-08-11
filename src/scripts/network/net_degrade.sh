#!/bin/sh
# rNET_degrade: NIC speed degrade via tc tbf (sync).
# Simulates a slow NIC by rate-limiting to speed_mbps (default 10).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_degrade-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        # Validate iface: alphanumeric, underscore, hyphen only
        case "$iface" in ''|*[!a-zA-Z0-9_-]*) echo "invalid iface: '$iface'" >&2; exit 1 ;; esac
        speed=${DCAT_PARAM_SPEED_MBPS:-10}
        case "$speed" in ''|*[!0-9]*) echo "speed_mbps must be a positive integer, got: '$speed'" >&2; exit 1 ;; esac
        [ "$speed" -ge 1 ] 2>/dev/null || { echo "speed_mbps must be >= 1, got: $speed" >&2; exit 1; }
        SIDECAR="/tmp/dcat-rNET_degrade-${iface}.sidecar"
        tc qdisc add dev "$iface" root tbf rate "${speed}mbit" burst "${speed}kbit" latency 400ms || { echo "$iface 已有 root qdisc ($(tc qdisc show dev "$iface" 2>/dev/null | grep root | head -1))" >&2; echo "请先清理: dcat clean --all 或 tc qdisc del dev $iface root" >&2; exit 1; }
        echo "$iface $speed" > "$SIDECAR"
        echo "degraded $iface to ${speed}Mbps (tc tbf)"
        ;;
    clean)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_degrade-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_degrade-}; v=${v%.sidecar}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            tc qdisc del dev "$iface" root 2>/dev/null
            rm -f "/tmp/dcat-rNET_degrade-${iface}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "restored [$ifaces] speed";
        else echo "restored speed (no active injection)"; fi
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces=$DCAT_PARAM_IFACE
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_degrade-*.sidecar; do
                [ -f "$sc" ] || continue
                ifaces="$ifaces $(awk '{print $1}' "$sc" 2>/dev/null)"
            done
        fi
        found=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            out=$(tc qdisc show dev "$iface" 2>/dev/null)
            match=$(echo "$out" | grep -E "qdisc tbf")
            [ -n "$match" ] && { echo "[$iface] $match"; found=1; }
        done
        [ "$found" = 1 ] && { echo "FAULT CONFIRMED"; exit 0; } || { echo "FAULT NOT ACTIVE"; exit 1; }
        ;;
esac
