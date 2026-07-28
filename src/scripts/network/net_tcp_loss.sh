#!/bin/sh
# rNET_tcp_loss: TCP packet loss via iptables DROP (sync).
port="${DCAT_PARAM_PORT:-}"
SIDECAR="/tmp/dcat-rNET_tcp_loss-${port}.rule"

case "${DCAT_OP:-inject}" in
    inject)
        port=${DCAT_PARAM_PORT:?missing required param: port}
        dir=${DCAT_PARAM_DIRECTION:-both}
        SIDECAR="/tmp/dcat-rNET_tcp_loss-${port}.rule"
        rc=0
        if [ "$dir" = "in" ] || [ "$dir" = "both" ]; then
            iptables -I INPUT -p tcp --dport "$port" -j DROP || rc=$?
        fi
        if [ "$dir" = "out" ] || [ "$dir" = "both" ]; then
            iptables -I OUTPUT -p tcp --sport "$port" -j DROP || rc=$?
        fi
        [ "$rc" -ne 0 ] && { echo "iptables -I failed (need root?)" >&2; exit 1; }
        echo "$port $dir" > "$SIDECAR"
        echo "applied tcp DROP on port $port ($dir)"
        ;;
    clean)
        port="${DCAT_PARAM_PORT:-}"
        SIDECAR="/tmp/dcat-rNET_tcp_loss-${port}.rule"
        rest=$(cat "$SIDECAR" 2>/dev/null || echo "$port both")
        dir=${DCAT_PARAM_DIRECTION:-${rest##* }}
        if [ "$dir" = "in" ] || [ "$dir" = "both" ]; then
            iptables -D INPUT -p tcp --dport "$port" -j DROP 2>/dev/null
        fi
        if [ "$dir" = "out" ] || [ "$dir" = "both" ]; then
            iptables -D OUTPUT -p tcp --sport "$port" -j DROP 2>/dev/null
        fi
        rm -f "$SIDECAR"
        echo "cleaned tcp DROP on port $port ($dir)"
        ;;
    query)
        port="${DCAT_PARAM_PORT:-}"
        dir=${DCAT_PARAM_DIRECTION:-both}
        found=0
        [ "$dir" = "in" ] || [ "$dir" = "both" ] && iptables -L INPUT -n 2>/dev/null | grep -qE "DROP.*dpt:$port([^0-9]|$)" && found=1
        [ "$dir" = "out" ] || [ "$dir" = "both" ] && iptables -L OUTPUT -n 2>/dev/null | grep -qE "DROP.*spt:$port([^0-9]|$)" && found=1
        iptables -L -n 2>/dev/null
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
