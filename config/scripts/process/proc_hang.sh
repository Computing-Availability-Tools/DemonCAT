#!/bin/sh
# rPROC_hang: SIGSTOP a process; clean sends SIGCONT.
# Env: DCAT_OP, DCAT_PARAM_PID
SIDECAR=/run/demoncat/rPROC_hang.pid
case "${DCAT_OP:-inject}" in
    inject)
        pid=${DCAT_PARAM_PID:?missing pid}
        if ! kill -STOP "$pid" 2>&1; then
            echo "kill -STOP $pid failed" >&2; exit 1
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$pid" > "$SIDECAR" 2>/dev/null
        echo "stopped pid $pid"
        ;;
    clean)
        pid=${DCAT_PARAM_PID:-$(cat "$SIDECAR" 2>/dev/null || echo "")}
        if [ -n "$pid" ]; then
            kill -CONT "$pid" 2>/dev/null
        fi
        rm -f "$SIDECAR" 2>/dev/null
        echo "continued pid $pid"
        ;;
    query)
        pid=${DCAT_PARAM_PID:-$(cat "$SIDECAR" 2>/dev/null || echo "")}
        if [ -z "$pid" ]; then
            echo "pid=(none) status=no_pid"
            exit 1
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "pid=$pid status=not_found"
            exit 1
        fi
        state=$(awk '/^State:/{print $2}' "/proc/$pid/status" 2>/dev/null)
        echo "pid=$pid state=${state:-unknown}"
        case "$state" in
            T*) exit 0 ;;
            *)  exit 1 ;;
        esac
        ;;
esac
