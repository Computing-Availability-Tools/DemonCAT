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
        if [ -n "$DCAT_PARAM_PORT" ]; then
            ports="$DCAT_PARAM_PORT"
        else
            ports=""
            for rf in /tmp/dcat-rNET_tcp_loss-*.rule; do
                [ -f "$rf" ] || continue
                p=${rf##*/dcat-rNET_tcp_loss-}; p=${p%.rule}
                ports="$ports $p"
            done
        fi
        cleaned=0
        for port in $ports; do
            [ -n "$port" ] || continue
            RF="/tmp/dcat-rNET_tcp_loss-${port}.rule"
            rest=$(cat "$RF" 2>/dev/null || echo "$port both")
            dir=${DCAT_PARAM_DIRECTION:-${rest##* }}
            dir=${dir:-both}
            if [ "$dir" = "in" ] || [ "$dir" = "both" ]; then
                iptables -D INPUT -p tcp --dport "$port" -j DROP 2>/dev/null
            fi
            if [ "$dir" = "out" ] || [ "$dir" = "both" ]; then
                iptables -D OUTPUT -p tcp --sport "$port" -j DROP 2>/dev/null
            fi
            rm -f "$RF"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "cleaned tcp DROP on [$ports]";
        else echo "cleaned tcp DROP (no active injection)"; fi
        ;;
    query)
        if [ -n "$DCAT_PARAM_PORT" ]; then
            ports=$DCAT_PARAM_PORT
        else
            ports=""
            for sc in /tmp/dcat-rNET_tcp_loss-*.rule; do
                [ -f "$sc" ] || continue
                read -r p _ < "$sc" 2>/dev/null
                [ -n "$p" ] && ports="$ports $p"
            done
        fi
        found=0
        for port in $ports; do
            [ -n "$port" ] || continue
            dir=${DCAT_PARAM_DIRECTION:-$(awk '{print $2}' "/tmp/dcat-rNET_tcp_loss-${port}.rule" 2>/dev/null)}
            dir=${dir:-both}
            if [ "$dir" = "in" ] || [ "$dir" = "both" ]; then
                m=$(iptables -L INPUT -n 2>/dev/null | grep -E "DROP.*dpt:$port([^0-9]|$)")
                [ -n "$m" ] && { echo "$m"; found=1; }
            fi
            if [ "$dir" = "out" ] || [ "$dir" = "both" ]; then
                m=$(iptables -L OUTPUT -n 2>/dev/null | grep -E "DROP.*spt:$port([^0-9]|$)")
                [ -n "$m" ] && { echo "$m"; found=1; }
            fi
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
