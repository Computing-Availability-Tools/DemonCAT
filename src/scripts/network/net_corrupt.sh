#!/bin/sh
# rNET_corrupt: inject packet corruption (tc netem corrupt).
# inject: tc qdisc add root netem corrupt <pct>%
# clean:  tc qdisc del root
# query:  tc qdisc show + grep netem corrupt

SIDECAR="/tmp/dcat-rNET_corrupt.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        pct=${DCAT_PARAM_CORRUPT_PCT:?missing required param: corrupt_pct}
        case "$pct" in *[!0-9]*|"") echo "corrupt_pct must be an integer" >&2; exit 1;; esac
        tc qdisc del dev "$iface" root 2>/dev/null || true
        tc qdisc add dev "$iface" root netem corrupt "$pct"% 2>/dev/null || { echo "tc add failed (need root? iface valid?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "injected packet corruption on $iface (${pct}%)"
        ;;
    clean)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null)}"
        [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
        tc qdisc del dev "$iface" root 2>/dev/null
        rm -f "$SIDECAR"
        echo "cleaned packet corruption on $iface"
        ;;
    query)
        iface="${DCAT_PARAM_IFACE:-$(cat "$SIDECAR" 2>/dev/null)}"
        out=$(tc -o qdisc show dev "$iface" 2>/dev/null)
        echo "$out"
        echo "$out" | grep -q "netem" && echo "$out" | grep -q "corrupt" && exit 0 || exit 1
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
