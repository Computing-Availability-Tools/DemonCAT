#!/bin/sh
# rNET_port_occupy: hold a TCP/UDP port (background, pidfile model).
port="${DCAT_PARAM_PORT:-}"
PIDFILE="/tmp/dcat-rNET_port_occupy-${port}.pid"

case "${DCAT_OP:-inject}" in
    inject)
        port=${DCAT_PARAM_PORT:?missing required param: port}
        proto=${DCAT_PARAM_PROTOCOL:-tcp}
        PIDFILE="/tmp/dcat-rNET_port_occupy-${port}.pid"
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE" 2>/dev/null); do kill "$pid" 2>/dev/null; done
            rm -f "$PIDFILE"
        fi
        if command -v python3 >/dev/null 2>&1; then
            python3 -c "
import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM if '$proto'=='tcp' else socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', $port))
if '$proto'=='tcp': s.listen(1)
while True: time.sleep(3600)
" >/dev/null 2>&1 &
            echo $! > "$PIDFILE"
            echo "occupied port $port ($proto), pid=$(cat "$PIDFILE")"
        else
            echo "python3 not found" >&2; exit 1
        fi
        ;;
    clean)
        if [ -n "$DCAT_PARAM_PORT" ]; then
            ports="$DCAT_PARAM_PORT"
        else
            ports=""
            for pf in /tmp/dcat-rNET_port_occupy-*.pid; do
                [ -f "$pf" ] || continue
                p=${pf##*/dcat-rNET_port_occupy-}; p=${p%.pid}
                ports="$ports $p"
            done
        fi
        cleaned=0
        for port in $ports; do
            [ -n "$port" ] || continue
            PIDFILE="/tmp/dcat-rNET_port_occupy-${port}.pid"
            if [ -f "$PIDFILE" ]; then
                kill "$(cat "$PIDFILE")" 2>/dev/null
                rm -f "$PIDFILE"
                cleaned=1
            fi
        done
        if [ "$cleaned" = 1 ]; then echo "released port [$ports]";
        else echo "released port (no active injection)"; fi
        ;;
    query)
        if command -v ss >/dev/null 2>&1; then
            out=$(ss -tulnp 2>/dev/null)
        else
            out=$(netstat -tulnp 2>/dev/null)
        fi
        ports=""
        if [ -n "$DCAT_PARAM_PORT" ]; then
            ports=$DCAT_PARAM_PORT
        else
            for pf in /tmp/dcat-rNET_port_occupy-*.pid; do
                [ -f "$pf" ] || continue
                pid=$(cat "$pf" 2>/dev/null)
                [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || continue
                p=${pf##*/dcat-rNET_port_occupy-}; p=${p%.pid}
                ports="$ports $p"
            done
        fi
        echo "$out"
        [ -z "$ports" ] && exit 1
        found=0
        for port in $ports; do
            [ -n "$port" ] && echo "$out" | grep -qE "[:.]${port}([^0-9]|$)" && found=1
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
