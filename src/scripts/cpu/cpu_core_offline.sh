#!/bin/sh
# rCPU_core_offline: offline CPU cores via sysfs.
# inject: echo 0 > /sys/devices/system/cpu<N>/online, write sidecar, exit
# clean:  read sidecar, echo 1 > online, exit
# query:  check actual online state of requested cores
# cpu0 is usually not offlinable; script skips and warns.

SIDECAR=/tmp/dcat-rCPU_core_offline.list

parse_cores() {
    echo "$1" | tr ',' '\n' | while IFS= read -r r; do
        [ -z "$r" ] && continue
        case "$r" in
            *-*)
                start=${r%%-*}
                end=${r##*-}
                n=$start
                while [ "$n" -le "$end" ]; do echo "$n"; n=$((n + 1)); done
                ;;
            *)
                echo "$r"
                ;;
        esac
    done
}

case "${DCAT_OP:-inject}" in
    inject)
        spec=${DCAT_PARAM_CORES:?missing required param: cores}
        offlist=""
        for n in $(parse_cores "$spec"); do
            path="/sys/devices/system/cpu/cpu$n/online"
            [ -w "$path" ] || { echo "cpu$n not offlinable (skipping)" >&2; continue; }
            if ! echo 0 > "$path" 2>/dev/null; then
                echo "offline cpu$n failed" >&2; continue
            fi
            offlist="$offlist $n"
        done
        echo "$offlist" > "$SIDECAR"
        echo "offlined cores:$offlist"
        ;;

    clean)
        offlist=$(cat "$SIDECAR" 2>/dev/null || echo "")
        onlist=""
        for n in $offlist; do
            path="/sys/devices/system/cpu/cpu$n/online"
            if [ -w "$path" ] && echo 1 > "$path" 2>/dev/null; then
                onlist="$onlist $n"
            fi
        done
        rm -f "$SIDECAR"
        echo "onlined cores:$onlist"
        ;;

    query)
        spec=${DCAT_PARAM_CORES:-0}
        any_offline=0
        printf 'core\tonline\tstatus\n'
        for n in $(parse_cores "$spec"); do
            path="/sys/devices/system/cpu/cpu$n/online"
            if [ -r "$path" ]; then
                v=$(cat "$path" 2>/dev/null)
            else
                v="-"
            fi
            if [ "$v" = "0" ]; then
                status="OFFLINE"; any_offline=1
            elif [ "$v" = "1" ]; then
                status="online"
            else
                status="n/a"
            fi
            printf '%s\t%s\t%s\n' "$n" "$v" "$status"
        done
        [ "$any_offline" -eq 1 ]
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2
        exit 1
        ;;
esac
