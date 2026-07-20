#!/bin/sh
# rNET_bw_limit: bandwidth limit via tc tbf.
# Env: DCAT_OP, DCAT_PARAM_IFACE, DCAT_PARAM_RATE_KBPS
SIDECAR=/run/demoncat/rNET_bw_limit.iface
case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing iface}
        rate=${DCAT_PARAM_RATE_KBPS:?missing rate_kbps}
        if ! tc qdisc add dev "$iface" root tbf rate "${rate}kbit" burst 32kbit latency 400ms 2>&1; then
            echo "tc add failed (need root/CAP_NET_ADMIN?)" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$iface" > "$SIDECAR" 2>/dev/null
        echo "applied ${rate}kbps limit on $iface"
        ;;
    clean)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "$SIDECAR" 2>/dev/null
        echo "cleaned bw limit on $iface"
        ;;
    query)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        out=$(tc qdisc show dev "$iface" 2>/dev/null)
        if echo "$out" | grep -qE "qdisc tbf"; then
            echo "FAULT CONFIRMED: tbf bandwidth limit active on $iface"
            echo "$out"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no tbf limit on $iface"
            echo "$out"
            exit 1
        fi
        ;;
esac
