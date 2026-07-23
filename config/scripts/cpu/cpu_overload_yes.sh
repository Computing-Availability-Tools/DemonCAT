#!/bin/sh
# rCPU_overload_yes: CPU overload via yes (per-core, has syscall overhead).
# Same as rCPU_overload but uses `yes` instead of `perl`.
# yes writes "y\n" to /dev/null repeatedly → ~30% us, ~70% sy per core.
#
# cores spec: "0,2,4" or "0-3"

PIDFILE="/tmp/dcat-rCPU_overload_yes.pid"

parse_cores() {
    echo "$1" | tr ',' '\n' | while IFS= read -r r; do
        [ -z "$r" ] && continue
        case "$r" in
            *-*)
                start=${r%%-*}; end=${r##*-}
                n=$start
                while [ "$n" -le "$end" ]; do echo "$n"; n=$((n + 1)); done
                ;;
            *) echo "$r" ;;
        esac
    done
}

case "${DCAT_OP:-inject}" in
    inject)
        spec=${DCAT_PARAM_CORES:?missing required param: cores}
        PIDFILE="/tmp/dcat-rCPU_overload_yes-${spec}.pid"
        pids=""
        for n in $(parse_cores "$spec"); do
            taskset -c "$n" yes >/dev/null 2>&1 &
            pids="$pids $!"
        done
        echo "$pids" > "$PIDFILE"
        echo "injected CPU overload (yes) on cores [$spec] (pids:$pids)"
        ;;

    clean)
        spec="${DCAT_PARAM_CORES:-}"
        PIDFILE="/tmp/dcat-rCPU_overload_yes-${spec}.pid"
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE"); do kill "$pid" 2>/dev/null; done
            rm -f "$PIDFILE"
            echo "cleaned CPU overload (yes) on cores [$spec]"
        else
            echo "no active injection for cores=$spec" >&2; exit 1
        fi
        ;;

    query)
        spec=${DCAT_PARAM_CORES:-0}
        yes_count=$(pgrep -x yes 2>/dev/null | wc -l)
        yes_count=${yes_count## }
        echo "requested_cores: $spec"
        echo "yes_processes: $yes_count"
        echo "--- per-core CPU (top) ---"
        top -bn1 2>/dev/null | head -7
        echo "--- process details ---"
        ps -eo pid,%cpu,psr,cmd 2>/dev/null | grep '[y]es' || echo "(none)"
        [ "$yes_count" -gt 0 ]
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2; exit 1
        ;;
esac
