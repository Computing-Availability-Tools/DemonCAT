#!/bin/sh
# rPROC_zstate: spawn N zombie children (background, pidfile model).
count="${DCAT_PARAM_COUNT:-}"
PIDFILE="/tmp/dcat-rPROC_zstate-${count}.pid"

case "${DCAT_OP:-inject}" in
    inject)
        count=${DCAT_PARAM_COUNT:?missing required param: count}
        PIDFILE="/tmp/dcat-rPROC_zstate-${count}.pid"
        (
            i=0
            while [ "$i" -lt "$count" ]; do
                ( exit 0 ) &
                i=$((i + 1))
            done
            while true; do sleep 3600; done
        ) >/dev/null 2>&1 &
        echo $! > "$PIDFILE"
        echo "spawned zombie parent (pid=$(cat "$PIDFILE"), $count zombies)"
        ;;
    clean)
        count="${DCAT_PARAM_COUNT:-}"
        PIDFILE="/tmp/dcat-rPROC_zstate-${count}.pid"
        if [ -f "$PIDFILE" ]; then
            kill "$(cat "$PIDFILE")" 2>/dev/null
            rm -f "$PIDFILE"
        fi
        echo "cleaned zombie parent"
        ;;
    query)
        count=${DCAT_PARAM_COUNT:-1}
        list=$(ps -eo pid,stat,cmd 2>/dev/null | awk '$2 ~ /^Z/')
        echo "expected_count=$count"
        if [ -n "$list" ]; then
            n=$(printf '%s\n' "$list" | awk 'END{print NR}')
            echo "zombie_count=$n"
            printf 'PID\tSTAT\tCMD\n'
            printf '%s\n' "$list"
            exit 0
        fi
        echo "zombie_count=0"
        exit 1
        ;;
esac
