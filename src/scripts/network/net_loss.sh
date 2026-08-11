#!/bin/sh
# rNET_loss: packet loss via tc netem (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_loss-${iface}.sidecar"

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
        pct=${DCAT_PARAM_LOSS_PCT:?missing required param: loss_pct}
        validate_iface "$iface" || exit 1
        case "$pct" in ''|*[!0-9]*) echo "loss_pct must be a number 0-100, got: '$pct'" >&2; exit 1 ;; esac
        [ "$pct" -ge 0 ] 2>/dev/null && [ "$pct" -le 100 ] 2>/dev/null || { echo "loss_pct must be 0-100, got: $pct" >&2; exit 1; }
        SIDECAR="/tmp/dcat-rNET_loss-${iface}.sidecar"
        tc qdisc add dev "$iface" root netem loss random "${pct}%" || { echo "$iface 已有 root qdisc ($(tc qdisc show dev "$iface" 2>/dev/null | grep root | head -1))" >&2; echo "请先清理: dcat clean --all 或 tc qdisc del dev $iface root" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${pct}% loss on $iface"
        ;;
    clean)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_loss-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_loss-}; v=${v%.sidecar}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            tc qdisc del dev "$iface" root 2>/dev/null
            rm -f "/tmp/dcat-rNET_loss-${iface}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "cleaned loss on [$ifaces]";
        else echo "cleaned loss (no active injection)"; fi
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces=$DCAT_PARAM_IFACE
        else
            ifaces=""
            for sc in /tmp/dcat-rNET_loss-*.sidecar; do
                [ -f "$sc" ] || continue
                ifaces="$ifaces $(cat "$sc" 2>/dev/null)"
            done
        fi
        found=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            out=$(tc qdisc show dev "$iface" 2>/dev/null)
            match=$(echo "$out" | grep -E "netem.*loss")
            [ -n "$match" ] && { echo "[$iface] $match"; found=1; }
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
