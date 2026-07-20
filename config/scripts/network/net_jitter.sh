#!/bin/sh
# rNET_jitter: delay + jitter via tc netem.
# Env: DCAT_OP, DCAT_PARAM_IFACE, DCAT_PARAM_DELAY_MS, DCAT_PARAM_JITTER_MS
SIDECAR=/run/demoncat/rNET_jitter.iface
case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing iface}
        delay=${DCAT_PARAM_DELAY_MS:?missing delay_ms}
        jitter=${DCAT_PARAM_JITTER_MS:?missing jitter_ms}
        if ! tc qdisc add dev "$iface" root netem delay "${delay}ms" "${jitter}ms" 2>&1; then
            echo "tc add failed (need root/CAP_NET_ADMIN?)" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$iface" > "$SIDECAR" 2>/dev/null
        echo "applied ${delay}ms +/- ${jitter}ms jitter on $iface"
        ;;
    clean)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "$SIDECAR" 2>/dev/null
        echo "cleaned jitter on $iface"
        ;;
    query)
        iface=${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null || echo eth0)}
        out=$(tc qdisc show dev "$iface" 2>/dev/null)
        if echo "$out" | grep -qE "netem.*delay [0-9]+[a-z]* [0-9]+[a-z]*"; then
            echo "FAULT CONFIRMED: netem jitter active on $iface"
            echo "$out"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no netem jitter on $iface"
            echo "$out"
            exit 1
        fi
        ;;
esac
