#!/bin/sh
# rCPU_core_hang: pin an RT-priority busy loop to each core, starving normal scheduling.
# inject: chrt -f 1 + taskset -c <core> busy loop per core; write per-core pidfile
# clean:  kill RT loops (glob all per-core pidfiles)
# query:  count alive RT loops
# Distinct from rCPU_overload (pure burn, non-RT) and rCPU_core_offline (sysfs down).

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

pidfile_for() { echo "/tmp/dcat-rCPU_core_hang-c$1.pid"; }

case "${DCAT_OP:-inject}" in
    inject)
        spec=${DCAT_PARAM_CORES:?missing required param: cores}
        case "$spec" in *[!0-9,-]*) echo "invalid cores spec '$spec'" >&2; exit 1;; esac

        # Test if RT scheduling is available (requires CAP_SYS_NICE)
        USE_CHRT=0
        if command -v chrt >/dev/null 2>&1 && chrt -f 1 true 2>/dev/null; then
            USE_CHRT=1
        fi

        pids=""
        for n in $(parse_cores "$spec"); do
            if [ "$USE_CHRT" = 1 ] && command -v perl >/dev/null 2>&1; then
                chrt -f 1 taskset -c "$n" perl -e '1 while 1' >/dev/null 2>&1 &
            elif [ "$USE_CHRT" = 1 ]; then
                chrt -f 1 taskset -c "$n" yes >/dev/null 2>&1 &
            elif command -v perl >/dev/null 2>&1; then
                taskset -c "$n" perl -e '1 while 1' >/dev/null 2>&1 &
            else
                taskset -c "$n" yes >/dev/null 2>&1 &
            fi
            echo "$!" > "$(pidfile_for "$n")"
            pids="$pids $!"
        done
        sleep 1
        alive=0
        for pid in $pids; do
            kill -0 "$pid" 2>/dev/null && alive=$((alive + 1))
        done
        total=$(echo "$pids" | wc -w)
        if [ "$alive" -lt "$total" ]; then
            echo "ERROR: only $alive/$total loops alive" >&2
            for pid in $pids; do kill "$pid" 2>/dev/null; done
            for n in $(parse_cores "$spec"); do rm -f "$(pidfile_for "$n")"; done
            exit 1
        fi
        echo "hung cores [$spec] ($alive/$total alive, pids:$pids)"
        ;;
    clean)
        spec="${DCAT_PARAM_CORES:-}"
        found=0
        if [ -n "$spec" ]; then
            for n in $(parse_cores "$spec"); do
                pf="$(pidfile_for "$n")"
                [ -f "$pf" ] || continue
                for pid in $(cat "$pf" 2>/dev/null); do
                    kill "$pid" 2>/dev/null
                done
                rm -f "$pf"
                found=1
            done
        else
            for pf in /tmp/dcat-rCPU_core_hang-c*.pid; do
                [ -f "$pf" ] || continue
                for pid in $(cat "$pf" 2>/dev/null); do
                    kill "$pid" 2>/dev/null
                done
                rm -f "$pf"
                found=1
            done
        fi
        [ "$found" = 1 ] && echo "cleaned core_hang" || { echo "no active core_hang" >&2; exit 1; }
        ;;
    query)
        spec="${DCAT_PARAM_CORES:-}"
        found=0
        n=0
        if [ -n "$spec" ]; then
            for c in $(parse_cores "$spec"); do
                pf="$(pidfile_for "$c")"
                [ -f "$pf" ] || continue
                for pid in $(cat "$pf" 2>/dev/null); do
                    kill -0 "$pid" 2>/dev/null && n=$((n + 1))
                done
                found=1
            done
        else
            for pf in /tmp/dcat-rCPU_core_hang-c*.pid; do
                [ -f "$pf" ] || continue
                for pid in $(cat "$pf" 2>/dev/null); do
                    kill -0 "$pid" 2>/dev/null && n=$((n + 1))
                done
                found=1
            done
        fi
        if [ "$found" = 1 ]; then
            echo "core_hang: $n RT procs alive"
            [ "$n" -gt 0 ] && exit 0 || exit 1
        else
            echo "no active core_hang"; exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
