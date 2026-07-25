#!/bin/sh
# rPROC_exit: kill -9 a process (inject-only, irreversible).
case "${DCAT_OP:-inject}" in
    inject)
        pid=${DCAT_PARAM_PID:?missing required param: pid}
        kill -9 "$pid" || { echo "kill -9 $pid failed (no such process or no permission?)" >&2; exit 1; }
        echo "killed pid $pid"
        ;;
    clean)
        echo "rPROC_exit is inject-only; nothing to clean" >&2
        exit 0
        ;;
    query)
        pid=${DCAT_PARAM_PID:?missing required param: pid}
        kill -0 "$pid" 2>/dev/null && { echo "pid $pid still running"; exit 1; } || { echo "pid $pid not running"; exit 0; }
        ;;
esac
