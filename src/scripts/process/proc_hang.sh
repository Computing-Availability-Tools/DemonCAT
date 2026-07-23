#!/bin/sh
# rPROC_hang: SIGSTOP a process; clean sends SIGCONT (sync).
pid="${DCAT_PARAM_PID:-}"
SIDECAR="/tmp/dcat-rPROC_hang-${pid}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        pid=${DCAT_PARAM_PID:?missing required param: pid}
        SIDECAR="/tmp/dcat-rPROC_hang-${pid}.sidecar"
        kill -STOP "$pid" || { echo "kill -STOP $pid failed" >&2; exit 1; }
        echo "$pid" > "$SIDECAR"
        echo "stopped pid $pid"
        ;;
    clean)
        pid="${DCAT_PARAM_PID:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        [ -n "$pid" ] || { echo "no pid to continue" >&2; exit 1; }
        kill -CONT "$pid" 2>/dev/null
        rm -f "/tmp/dcat-rPROC_hang-${pid}.sidecar"
        echo "continued pid $pid"
        ;;
    query)
        pid="${DCAT_PARAM_PID:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        [ -n "$pid" ] || { echo "no pid"; exit 1; }
        kill -0 "$pid" 2>/dev/null || { echo "pid $pid not found"; exit 1; }
        state=$(awk '/^State:/{print $2}' "/proc/$pid/status" 2>/dev/null)
        echo "pid=$pid state=${state:-unknown}"
        case "$state" in T*) exit 0;; *) exit 1;; esac
        ;;
esac
