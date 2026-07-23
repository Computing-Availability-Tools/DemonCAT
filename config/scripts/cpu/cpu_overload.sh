#!/bin/sh
# rCPU_overload: CPU overload (multi-core burn)
# inject: spawn N `yes` processes, write pidfile, exit immediately
# clean:  read pidfile, kill yes processes, remove pidfile, exit
# query:  check actual CPU state (yes process count, cpu usage)

# pidfile includes params for concurrent-injection isolation
PIDFILE="/tmp/dcat-rCPU_overload-cores${DCAT_PARAM_CORES:-0}.pid"

case "${DCAT_OP:-inject}" in
    inject)
        cores=${DCAT_PARAM_CORES:?missing required param: cores}
        pids=""
        i=0
        while [ "$i" -lt "$cores" ]; do
            yes >/dev/null 2>&1 &
            pids="$pids $!"
            i=$((i + 1))
        done
        echo "$pids" > "$PIDFILE"
        echo "injected CPU overload: $cores cores (pids:$pids)"
        ;;

    clean)
        cores=${DCAT_PARAM_CORES:-0}
        PIDFILE="/tmp/dcat-rCPU_overload-cores${cores}.pid"
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE"); do
                kill "$pid" 2>/dev/null
            done
            rm -f "$PIDFILE"
            echo "cleaned CPU overload: $cores cores"
        else
            echo "no active injection for cores=$cores" >&2
            exit 1
        fi
        ;;

    query)
        cores=${DCAT_PARAM_CORES:-1}
        yes_count=$(pgrep -x yes 2>/dev/null | wc -l)
        yes_count=${yes_count## }
        echo "requested_cores: $cores"
        echo "yes_processes: $yes_count"
        echo "--- cpu usage ---"
        if command -v mpstat >/dev/null 2>&1; then
            mpstat 1 1 2>/dev/null | tail -5
        else
            top -bn1 2>/dev/null | head -5
        fi
        [ "$yes_count" -gt 0 ]
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2
        exit 1
        ;;
esac
