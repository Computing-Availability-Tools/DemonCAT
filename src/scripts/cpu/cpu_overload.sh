#!/bin/sh
# rCPU_overload: CPU overload (per-core burn with taskset pinning, pure user-space).
# inject: for each specified core, spawn taskset -c <core> perl burn + pidfile, exit
# clean:  read pidfile, kill processes, exit
# query:  check burn process count + per-core CPU usage
#
# cores spec: "0,2,4" or "0-3" or "0-3,7" (same format as rCPU_core_offline)
# Uses `perl -e '1 while 1'` for pure user-space (us) CPU burn (no syscall overhead).
# Falls back to `yes >/dev/null` if perl not available (will have ~60% sy overhead).

PIDFILE="/tmp/dcat-rCPU_overload.pid"

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
        PIDFILE="/tmp/dcat-rCPU_overload-${spec}.pid"
        pids=""
        for n in $(parse_cores "$spec"); do
            if command -v perl >/dev/null 2>&1; then
                taskset -c "$n" perl -e '1 while 1' >/dev/null 2>&1 &
            else
                taskset -c "$n" yes >/dev/null 2>&1 &
            fi
            pids="$pids $!"
        done
        echo "$pids" > "$PIDFILE"
        echo "injected CPU overload on cores [$spec] (pids:$pids)"
        ;;

    clean)
        spec="${DCAT_PARAM_CORES:-}"
        PIDFILE="/tmp/dcat-rCPU_overload-${spec}.pid"
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE"); do
                kill "$pid" 2>/dev/null
            done
            rm -f "$PIDFILE"
            echo "cleaned CPU overload on cores [$spec]"
        else
            echo "no active injection for cores=$spec" >&2
            exit 1
        fi
        ;;

    query)
        spec=${DCAT_PARAM_CORES:-0}
        burn_count=$(pgrep -f 'perl -e' 2>/dev/null | wc -l)
        yes_count=$(pgrep -x yes 2>/dev/null | wc -l)
        burn_count=${burn_count## }
        yes_count=${yes_count## }
        total=$((burn_count + yes_count))
        echo "requested_cores: $spec"
        echo "burn_processes: $total"
        echo "--- per-core CPU ---"
        if command -v mpstat >/dev/null 2>&1; then
            mpstat -P ALL 1 1 2>/dev/null | tail -n +4
        else
            echo "(mpstat unavailable — apt install sysstat for per-core view)"
        fi
        echo "--- burn process details ---"
        ps -eo pid,%cpu,psr,cmd 2>/dev/null | grep -E 'PID|[p]erl|[y]es' || echo "(none)"
        [ "$total" -gt 0 ]
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2
        exit 1
        ;;
esac
