#!/bin/sh
# rNET_link_flap: loop link down/up (background, pidfile model).
iface="${DCAT_PARAM_IFACE:-}"
PIDFILE="/tmp/dcat-rNET_link_flap-${iface}.pid"

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing required param: iface}
        cycle=${DCAT_PARAM_CYCLE_SEC:-2}
        count=${DCAT_PARAM_COUNT:-10}
        PIDFILE="/tmp/dcat-rNET_link_flap-${iface}.pid"
        (
            i=0
            while [ "$i" -lt "$count" ]; do
                ip link set dev "$iface" down 2>/dev/null
                sleep "$cycle"
                ip link set dev "$iface" up 2>/dev/null
                sleep "$cycle"
                i=$((i + 1))
            done
        ) >/dev/null 2>&1 &
        echo $! > "$PIDFILE"
        echo "started link flap on $iface (cycle=${cycle}s count=$count pid=$(cat "$PIDFILE"))"
        ;;
    clean)
        iface="${DCAT_PARAM_IFACE:-}"
        PIDFILE="/tmp/dcat-rNET_link_flap-${iface}.pid"
        if [ -f "$PIDFILE" ]; then
            kill "$(cat "$PIDFILE")" 2>/dev/null
            rm -f "$PIDFILE"
        fi
        [ -n "$iface" ] && ip link set dev "$iface" up 2>/dev/null
        echo "stopped link flap, ensured $iface up"
        ;;
    query)
        iface="${DCAT_PARAM_IFACE:-}"
        PIDFILE="/tmp/dcat-rNET_link_flap-${iface}.pid"
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "link flap running on $iface (pid=$(cat "$PIDFILE"))"
            exit 0
        else
            echo "no link flap on $iface"
            exit 1
        fi
        ;;
esac
