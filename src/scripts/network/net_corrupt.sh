#!/bin/sh
# rNET_corrupt: inject packet corruption (tc netem corrupt).
# inject: tc qdisc add root netem corrupt <pct>%
# clean:  tc qdisc del root
# query:  tc qdisc show + grep netem corrupt

SIDECAR_PFX="/tmp/dcat-rNET_corrupt"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        pct=${DCAT_PARAM_CORRUPT_PCT:?missing required param: corrupt_pct}
        case "$pct" in *[!0-9]*|"") echo "corrupt_pct must be an integer" >&2; exit 1;; esac
        safe=$(echo "$iface" | tr -c 'a-zA-Z0-9' '_')
        SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
        tc qdisc del dev "$iface" root 2>/dev/null || true
        tc qdisc add dev "$iface" root netem corrupt "$pct"% 2>/dev/null || { echo "tc add failed (need root? iface valid?)" >&2; exit 1; }
        echo "$iface" > "$SIDECAR"
        echo "injected packet corruption on $iface (${pct}%)"
        ;;
    clean)
        if [ -n "${DCAT_PARAM_IFACE:-}" ]; then
            safe=$(echo "$DCAT_PARAM_IFACE" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            iface=$(cat "$SIDECAR" 2>/dev/null)
            [ -n "$iface" ] || { echo "no iface to clean" >&2; exit 1; }
            tc qdisc del dev "$iface" root 2>/dev/null
            rm -f "$SIDECAR"
            echo "cleaned packet corruption on $iface"
        else
            found=0
            for f in "${SIDECAR_PFX}-"*.sidecar; do
                [ -f "$f" ] || continue
                found=1
                iface=$(cat "$f" 2>/dev/null)
                [ -n "$iface" ] && tc qdisc del dev "$iface" root 2>/dev/null
                rm -f "$f"
            done
            if [ "$found" -eq 1 ]; then
                echo "cleaned all net_corrupt"
            else
                echo "no active net_corrupt" >&2; exit 1
            fi
        fi
        ;;
    query)
        if [ -n "${DCAT_PARAM_IFACE:-}" ]; then
            safe=$(echo "$DCAT_PARAM_IFACE" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            iface=$(cat "$SIDECAR" 2>/dev/null)
            [ -n "$iface" ] || { echo "no active net_corrupt"; exit 1; }
            out=$(tc -o qdisc show dev "$iface" 2>/dev/null)
            echo "$out"
            echo "$out" | grep -q "netem" && echo "$out" | grep -q "corrupt" && exit 0 || exit 1
        else
            active=0
            for f in "${SIDECAR_PFX}-"*.sidecar; do
                [ -f "$f" ] || continue
                iface=$(cat "$f" 2>/dev/null)
                [ -n "$iface" ] || continue
                out=$(tc -o qdisc show dev "$iface" 2>/dev/null)
                if echo "$out" | grep -q "netem" && echo "$out" | grep -q "corrupt"; then
                    echo "$iface: $out"
                    active=1
                fi
            done
            [ "$active" -eq 1 ] && exit 0 || exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
