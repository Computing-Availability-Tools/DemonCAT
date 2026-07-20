#!/bin/sh
# rNET_port_occupy: hold a TCP/UDP port (background).
# Env: DCAT_OP, DCAT_PARAM_PORT, DCAT_PARAM_PROTOCOL (default tcp)
cleanup() { trap - TERM INT; kill 0 2>/dev/null; exit 0; }
trap cleanup TERM INT

case "${DCAT_OP:-inject}" in
    inject)
        port=${DCAT_PARAM_PORT:?missing port}
        proto=${DCAT_PARAM_PROTOCOL:-tcp}
        # use python3 if available (portable socket hold); fallback to nc
        if command -v python3 >/dev/null 2>&1; then
            python3 -c "
import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM if '$proto'=='tcp' else socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', $port))
if '$proto'=='tcp': s.listen(1)
while True: time.sleep(3600)
" &
            wait
        else
            # fallback: socat/nc; if neither, error
            (command -v socat >/dev/null && exec socat - TCP-LISTEN:$port,reuseaddr,fork) \
                || (command -v nc >/dev/null && exec nc -l -p $port) \
                || { echo "no python3/socat/nc to hold port" >&2; exit 1; }
        fi
        ;;
    clean)
        cleanup
        ;;
    query)
        port=${DCAT_PARAM_PORT:-}
        proto=${DCAT_PARAM_PROTOCOL:-tcp}
        if [ -z "$port" ]; then
            echo "FAULT NOT ACTIVE: no port specified"
            exit 1
        fi
        if command -v ss >/dev/null 2>&1; then
            out=$(ss -tulnp 2>/dev/null)
        else
            out=$(netstat -tulnp 2>/dev/null)
        fi
        pat="[:.]${port}([^0-9]|\$)"
        if echo "$out" | grep -qE "$pat"; then
            echo "FAULT CONFIRMED: port $port ($proto) is occupied"
            echo "$out" | grep -E "$pat"
            exit 0
        else
            echo "FAULT NOT ACTIVE: port $port ($proto) is free"
            exit 1
        fi
        ;;
esac
