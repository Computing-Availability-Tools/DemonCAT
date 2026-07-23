#!/bin/sh
# rNET_down: NIC down via ip link (sync).
iface="${DCAT_PARAM_IFACE:-}"
SIDECAR="/tmp/dcat-rNET_down-${iface}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        SIDECAR="/tmp/dcat-rNET_down-${iface}.sidecar"
        ip link set dev "$iface" down || { echo "ip link set down failed (need root?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "brought $iface down"
        ;;
    clean)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
        ip link set dev "$iface" up 2>/dev/null
        rm -f "/tmp/dcat-rNET_down-${iface}.sidecar"
        echo "brought $iface up"
        ;;
    query)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        out=$(ip -o link show dev "$iface" 2>/dev/null)
        echo "$out"
        echo "$out" | grep -q "state DOWN" && exit 0 || exit 1
        ;;
esac
