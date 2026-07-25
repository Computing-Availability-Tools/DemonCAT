#!/bin/sh
# rNET_port_occupy: hold a TCP/UDP port (background, pidfile model).
port="${DCAT_PARAM_PORT:-}"
PIDFILE="/tmp/dcat-rNET_port_occupy-${port}.pid"

case "${DCAT_OP:-inject}" in
    inject)
        port=${DCAT_PARAM_PORT:?missing required param: port}
        proto=${DCAT_PARAM_PROTOCOL:-tcp}
        PIDFILE="/tmp/dcat-rNET_port_occupy-${port}.pid"
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
        port="${DCAT_PARAM_PORT:-}"
        PIDFILE="/tmp/dcat-rNET_port_occupy-${port}.pid"
        if [ -f "$PIDFILE" ]; then
            kill "$(cat "$PIDFILE")" 2>/dev/null
            rm -f "$PIDFILE"
            echo "released port $port"
        else
            echo "no active injection for port=$port" >&2; exit 1
        fi
        ;;
    query)
        port=${DCAT_PARAM_PORT:-}
        if command -v ss >/dev/null 2>&1; then
            out=$(ss -tulnp 2>/dev/null)
        else
            out=$(netstat -tulnp 2>/dev/null)
        fi
        echo "$out"
        echo "$out" | grep -qE "[:.]${port}([^0-9]|$)" && exit 0 || exit 1
        ;;
esac
