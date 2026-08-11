#!/bin/sh
# rPROC_fork_bomb: create N child processes (controlled fork bomb).
# inject: spawn a supervisor that forks N sleepers; write pidfile
# clean:  kill children + supervisor
# query:  count children

PIDFILE="/tmp/dcat-rPROC_fork_bomb.pid"

case "${DCAT_OP:-inject}" in
    inject)
        count=${DCAT_PARAM_COUNT:?missing required param: count}
        case "$count" in *[!0-9]*|"") echo "count must be an integer" >&2; exit 1;; esac
        [ "$count" -lt 1 ] && { echo "count must be >= 1" >&2; exit 1; }
        (
            trap 'kill $(jobs -p) 2>/dev/null' TERM
            i=0
            while [ "$i" -lt "$count" ]; do
                sleep 3600 &
                i=$((i + 1))
            done
            wait
        ) >/dev/null 2>&1 &
        pid=$!
        echo "$pid" > "$PIDFILE"
        echo "fork_bomb: spawned $count children (supervisor pid $pid)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            pkill -P "$pid" 2>/dev/null || true
            kill "$pid" 2>/dev/null || true
            rm -f "$PIDFILE"
            echo "cleaned fork_bomb (supervisor $pid)"
        else
            echo "no active fork_bomb" >&2; exit 1
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            pid=$(cat "$PIDFILE")
            n=$(pgrep -P "$pid" 2>/dev/null | wc -l)
            echo "fork_bomb supervisor pid=$pid children=$n"
            exit 0
        else
            echo "no active fork_bomb"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
