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
        if [ -n "$DCAT_PARAM_PID" ]; then
            pids="$DCAT_PARAM_PID"
        else
            pids=""
            for sc in /tmp/dcat-rPROC_hang-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rPROC_hang-}; v=${v%.sidecar}
                pids="$pids $v"
            done
        fi
        cleaned=0
        for pid in $pids; do
            [ -n "$pid" ] || continue
            kill -CONT "$pid" 2>/dev/null
            rm -f "/tmp/dcat-rPROC_hang-${pid}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "continued [$pids]";
        else echo "continued (no active injection)"; fi
        ;;
    query)
        pids=""
        if [ -n "$DCAT_PARAM_PID" ]; then
            pids=$DCAT_PARAM_PID
        else
            for sc in /tmp/dcat-rPROC_hang-*.sidecar; do
                [ -f "$sc" ] || continue
                pids="$pids $(cat "$sc" 2>/dev/null)"
            done
        fi
        [ -z "$pids" ] && { echo "no pid (no active rPROC_hang injection)"; exit 1; }
        found=0
        for pid in $pids; do
            [ -n "$pid" ] || continue
            kill -0 "$pid" 2>/dev/null || { echo "pid $pid not found"; continue; }
            state=$(awk '/^State:/{print $2}' "/proc/$pid/status" 2>/dev/null)
            echo "pid=$pid state=${state:-unknown}"
            case "$state" in T*) found=1;; esac
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;
esac
