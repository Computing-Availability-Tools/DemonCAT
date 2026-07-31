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
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE" 2>/dev/null); do kill "$pid" 2>/dev/null; done
            rm -f "$PIDFILE"
        fi
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
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            ifaces="$DCAT_PARAM_IFACE"
        else
            ifaces=""
            for pf in /tmp/dcat-rNET_link_flap-*.pid; do
                [ -f "$pf" ] || continue
                v=${pf##*/dcat-rNET_link_flap-}; v=${v%.pid}
                ifaces="$ifaces $v"
            done
        fi
        cleaned=0
        for iface in $ifaces; do
            [ -n "$iface" ] || continue
            PIDFILE="/tmp/dcat-rNET_link_flap-${iface}.pid"
            if [ -f "$PIDFILE" ]; then
                kill "$(cat "$PIDFILE")" 2>/dev/null
                rm -f "$PIDFILE"
            fi
            ip link set dev "$iface" up 2>/dev/null
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "stopped link flap on [$ifaces]";
        else echo "stopped link flap (no active injection)"; fi
        ;;
    query)
        if [ -n "$DCAT_PARAM_IFACE" ]; then
            pfs="/tmp/dcat-rNET_link_flap-${DCAT_PARAM_IFACE}.pid"
        else
            pfs="/tmp/dcat-rNET_link_flap-*.pid"
        fi
        found=0
        for pf in $pfs; do
            [ -f "$pf" ] || continue
            pid=$(cat "$pf" 2>/dev/null)
            [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || continue
            ifc=${pf##*/dcat-rNET_link_flap-}; ifc=${ifc%.pid}
            echo "link flap running on $ifc (pid=$pid)"
            found=1
        done
        [ "$found" = 1 ] && exit 0 || { echo "no link flap running"; exit 1; }
        ;;
esac
