#!/bin/sh
# rNET_delay: network egress delay via tc netem (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_delay-${iface}.sidecar"

# Validate iface: alphanumeric, underscore, hyphen only (no command injection)
validate_iface() {
    case "$1" in
        ''|*[!a-zA-Z0-9_-]*) echo "invalid iface name: '$1' (alphanumeric, underscore, hyphen only)" >&2; return 1 ;;
    esac
    return 0
}

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        delay=${DCAT_PARAM_DELAY_MS:?missing required param: delay_ms}
        validate_iface "$iface" || exit 1
        case "$delay" in ''|*[!0-9]*) echo "delay_ms must be a positive integer, got: '$delay'" >&2; exit 1 ;; esac
        SIDECAR="/tmp/dcat-rNET_delay-${iface}.sidecar"
        tc qdisc add dev "$iface" root netem delay "${delay}ms" || { echo "$iface 已有 root qdisc ($(tc qdisc show dev "$iface" 2>/dev/null | grep root | head -1))" >&2; echo "请先清理: dcat clean --all 或 tc qdisc del dev $iface root" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${delay}ms delay on $iface"
        ;;
    clean)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_delay-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_delay-}; v=${v%.sidecar}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            tc qdisc del dev "$iface" root 2>/dev/null
            rm -f "/tmp/dcat-rNET_delay-${iface}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "cleaned delay on [$ifaces]";
        else echo "cleaned delay (no active injection)"; fi
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces=$DCAT_PARAM_IFACE
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_delay-*.sidecar; do
                [ -f "$sc" ] || continue
                ifaces="$ifaces $(cat "$sc" 2>/dev/null)"
            done
        fi
        found=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            out=$(tc qdisc show dev "$iface" 2>/dev/null)
            # 只匹配纯 delay netem (delay 在行尾), 排除 jitter/reorder
            match=$(echo "$out" | grep -E "netem.*delay [0-9.]+[a-z]*[[:space:]]*$")
            if [ -n "$match" ]; then
                echo "[$iface] $match"
                found=1
            else
                echo "[$iface] (no delay injection)"
            fi
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
