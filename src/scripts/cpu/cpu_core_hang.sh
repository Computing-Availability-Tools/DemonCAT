#!/bin/sh
# rCPU_core_hang: pin an RT-priority busy loop to each core, starving normal scheduling.
# inject: chrt -f 99 + taskset -c <core> busy loop per core; write pidfile
# clean:  kill RT loops
# query:  count alive RT loops
# Distinct from rCPU_overload (pure burn, non-RT) and rCPU_core_offline (sysfs down).

PIDFILE="/tmp/dcat-rCPU_core_hang.pid"

parse_cores() {
    echo "$1" | tr ',' '\n' | while IFS= read -r r; do
        [ -z "$r" ] && continue
        case "$r" in
            *-*)
                start=${r%%-*}; end=${r##*-}; n=$start
                while [ "$n" -le "$end" ]; do echo "$n"; n=$((n + 1)); done
                ;;
            *) echo "$r" ;;
        esac
    done
}

case "${DCAT_OP:-inject}" in
    inject)
        spec=${DCAT_PARAM_CORES:?missing required param: cores}
        case "$spec" in *[!0-9,-]*) echo "invalid cores spec '$spec'" >&2; exit 1;; esac
        pids=""
        for n in $(parse_cores "$spec"); do
            if command -v chrt >/dev/null 2>&1; then
                chrt -f 99 taskset -c "$n" sh -c 'while :; do :; done' >/dev/null 2>&1 &
            else
                # fallback: non-RT pin (less effective hang)
                taskset -c "$n" sh -c 'while :; do :; done' >/dev/null 2>&1 &
            fi
            pids="$pids $!"
        done
        echo "$pids" > "$PIDFILE"
        echo "hung cores [$spec] (pids:$pids)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE"); do kill "$pid" 2>/dev/null; done
            rm -f "$PIDFILE"
            echo "cleaned core_hang"
        else
            echo "no active core_hang" >&2; exit 1
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ]; then
            n=0
            for pid in $(cat "$PIDFILE"); do kill -0 "$pid" 2>/dev/null && n=$((n + 1)); done
            echo "core_hang: $n RT procs alive"
            [ "$n" -gt 0 ] && exit 0 || exit 1
        else
            echo "no active core_hang"; exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
