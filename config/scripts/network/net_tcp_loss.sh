#!/bin/sh
# rNET_tcp_loss: TCP packet loss via iptables DROP.
# Env: DCAT_OP, DCAT_PARAM_PORT, DCAT_PARAM_DIRECTION (default both: in,out)
SIDECAR=/run/demoncat/rNET_tcp_loss.rule
case "${DCAT_OP:-inject}" in
    inject)
        port=${DCAT_PARAM_PORT:?missing port}
        dir=${DCAT_PARAM_DIRECTION:-both}
        rc=0
        if [ "$dir" = "in" ] || [ "$dir" = "both" ]; then
            iptables -I INPUT -p tcp --dport "$port" -j DROP || rc=$?
        fi
        if [ "$dir" = "out" ] || [ "$dir" = "both" ]; then
            iptables -I OUTPUT -p tcp --sport "$port" -j DROP || rc=$?
        fi
        if [ "$rc" -ne 0 ]; then
            echo "iptables -I failed (need root?)" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$port $dir" > "$SIDECAR" 2>/dev/null
        echo "applied tcp DROP on port $port ($dir)"
        ;;
    clean)
        rest=$(cat "$SIDECAR" 2>/dev/null || echo "$DCAT_PARAM_PORT both")
        port=${DCAT_PARAM_PORT:-${rest%% *}}
        dir=${DCAT_PARAM_DIRECTION:-${rest##* }}
        [ "$dir" = "in" ] || [ "$dir" = "both" ] && iptables -D INPUT -p tcp --dport "$port" -j DROP 2>/dev/null
        [ "$dir" = "out" ] || [ "$dir" = "both" ] && iptables -D OUTPUT -p tcp --sport "$port" -j DROP 2>/dev/null
        rm -f "$SIDECAR" 2>/dev/null
        echo "cleaned tcp DROP on port $port ($dir)"
        ;;
    query)
        rest=$(cat "$SIDECAR" 2>/dev/null || echo "$DCAT_PARAM_PORT both")
        port=${DCAT_PARAM_PORT:-${rest%% *}}
        dir=${DCAT_PARAM_DIRECTION:-${rest##* }}
        [ -n "$port" ] || { echo "FAULT NOT ACTIVE: no port specified"; exit 1; }
        [ -n "$dir" ] || dir=both
        found=0
        pat_in="DROP.*dpt:$port([^0-9]|\$)"
        pat_out="DROP.*spt:$port([^0-9]|\$)"
        if [ "$dir" = "in" ] || [ "$dir" = "both" ]; then
            r=$(iptables -L INPUT -n 2>/dev/null)
            m=$(echo "$r" | grep -E "$pat_in")
            if [ -n "$m" ]; then
                found=1
                echo "[INPUT] DROP rule for port $port:"
                echo "$m"
            fi
        fi
        if [ "$dir" = "out" ] || [ "$dir" = "both" ]; then
            r=$(iptables -L OUTPUT -n 2>/dev/null)
            m=$(echo "$r" | grep -E "$pat_out")
            if [ -n "$m" ]; then
                found=1
                echo "[OUTPUT] DROP rule for port $port:"
                echo "$m"
            fi
        fi
        if [ "$found" = 1 ]; then
            echo "FAULT CONFIRMED: tcp DROP on port $port ($dir)"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no tcp DROP on port $port ($dir)"
            exit 1
        fi
        ;;
esac
