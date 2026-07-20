#!/bin/sh
# rNET_link_flap: loop link down/up (background, count-based).
# Env: DCAT_OP, DCAT_PARAM_IFACE, DCAT_PARAM_CYCLE_SEC (default 2), DCAT_PARAM_COUNT (default 10)
cleanup() {
    trap - TERM INT
    iface=${DCAT_PARAM_IFACE:-eth0}
    ip link set dev "$iface" up 2>/dev/null
    kill 0 2>/dev/null
    exit 0
}
trap cleanup TERM INT

case "${DCAT_OP:-inject}" in
    inject)
        iface=${DCAT_PARAM_IFACE:?missing iface}
        cycle=${DCAT_PARAM_CYCLE_SEC:-2}
        count=${DCAT_PARAM_COUNT:-10}
        i=0
        while [ "$i" -lt "$count" ]; do
            ip link set dev "$iface" down 2>/dev/null
            sleep "$cycle"
            ip link set dev "$iface" up 2>/dev/null
            sleep "$cycle"
            i=$((i + 1))
        done
        cleanup
        ;;
    clean)
        cleanup
        ;;
    query)
        iface=${DCAT_PARAM_IFACE:-eth0}
        if command -v pgrep >/dev/null 2>&1; then
            procs=$(pgrep -af net_link_flap 2>/dev/null | grep -vE "^$$ ")
        else
            procs=$(ps -eo pid,cmd 2>/dev/null | grep "[n]et_link_flap" | grep -vE "^[[:space:]]*$$[[:space:]]")
        fi
        if [ -n "$procs" ]; then
            echo "FAULT CONFIRMED: link flap running on $iface"
            echo "$procs"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no link flap running on $iface"
            exit 1
        fi
        ;;
esac
