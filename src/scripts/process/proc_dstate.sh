#!/bin/sh
# rPROC_dstate: spawn N processes doing uninterruptible IO (background, pidfile model).
count="${DCAT_PARAM_COUNT:-}"
PIDFILE="/tmp/dcat-rPROC_dstate-${count}.pid"

case "${DCAT_OP:-inject}" in
    inject)
        count=${DCAT_PARAM_COUNT:?missing required param: count}
        PIDFILE="/tmp/dcat-rPROC_dstate-${count}.pid"
        pids=""
        i=0
        while [ "$i" -lt "$count" ]; do
            ( while true; do dd if=/dev/zero of="/tmp/dcat.dstate.$$.$i" bs=1M count=100 conv=fdatasync 2>/dev/null || sleep 1; done ) >/dev/null 2>&1 &
            pids="$pids $!"
            i=$((i + 1))
        done
        echo "$pids" > "$PIDFILE"
        echo "spawned $count dstate workers (pids:$pids)"
        ;;
    clean)
        count="${DCAT_PARAM_COUNT:-}"
        PIDFILE="/tmp/dcat-rPROC_dstate-${count}.pid"
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE"); do
                kill "$pid" 2>/dev/null
            done
            rm -f "$PIDFILE"
        fi
        rm -f /tmp/dcat.dstate.* 2>/dev/null
        echo "cleaned dstate workers"
        ;;
    query)
        count=${DCAT_PARAM_COUNT:-1}
        list=$(ps -eo pid,stat,cmd 2>/dev/null | awk '$2 ~ /^D/')
        echo "expected_count=$count"
        if [ -n "$list" ]; then
            n=$(printf '%s\n' "$list" | awk 'END{print NR}')
            echo "dstate_count=$n"
            printf 'PID\tSTAT\tCMD\n'
            printf '%s\n' "$list"
            exit 0
        fi
        echo "dstate_count=0"
        exit 1
        ;;
esac
