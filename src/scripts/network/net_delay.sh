#!/bin/sh
# rNET_delay: network egress delay via tc netem (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_delay-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        delay=${DCAT_PARAM_DELAY_MS:?missing required param: delay_ms}
        SIDECAR="/tmp/dcat-rNET_delay-${iface}.sidecar"
        tc qdisc add dev "$iface" root netem delay "${delay}ms" || { echo "tc add failed (need root?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "applied ${delay}ms delay on $iface"
        ;;
    clean)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "/tmp/dcat-rNET_delay-${iface}.sidecar"
        echo "cleaned delay on $iface"
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
            # 只匹配纯 delay netem (delay 值在行尾), 排除 jitter/reorder; 不匹配则不输出 (查不到)
            match=$(echo "$out" | grep -E "netem.*delay [0-9]+[a-z]*[[:space:]]*$")
            [ -n "$match" ] && { echo "$match"; found=1; }
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
