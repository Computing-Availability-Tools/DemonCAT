#!/bin/sh
# rPROC_zstate: turn a real process into a zombie by killing it.
# inject: kill -9 $pid → process exits → zombie if parent doesn't reap
# clean:  kill the parent → zombie reparented to init → init reaps
# query:  check if $pid is in Z state
# Note: after clean, target process is dead and cannot be restored.

case "${DCAT_OP:-inject}" in
    inject)
        pid=${DCAT_PARAM_PID:?missing required param: pid}
        if [ ! -d "/proc/$pid" ]; then
            echo "pid $pid not found" >&2; exit 1
        fi
        ppid=$(awk '/^PPid:/{print $2}' /proc/$pid/status 2>/dev/null)
        [ -z "$ppid" ] && ppid=0
        kill -9 "$pid" 2>/dev/null || { echo "kill pid $pid failed" >&2; exit 1; }
        SIDECAR="/tmp/dcat-rPROC_zstate-$pid.info"
        printf '%s %s\n' "$pid" "$ppid" > "$SIDECAR"
        sleep 0.2
        state=$(awk '/^State:/{print $2}' /proc/$pid/status 2>/dev/null)
        if [ "$state" = "Z" ]; then
            echo "process $pid killed → zombie (parent $ppid not reaping)"
        else
            echo "process $pid killed (parent $ppid reaped immediately, no zombie persisted)"
        fi
        ;;

    clean)
        pid="${DCAT_PARAM_PID:-}"
        SIDECAR="/tmp/dcat-rPROC_zstate-$pid.info"
        if [ -f "$SIDECAR" ]; then
            read -r zpid ppid < "$SIDECAR"
            state=$(awk '/^State:/{print $2}' /proc/$zpid/status 2>/dev/null)
            if [ "$state" = "Z" ]; then
                kill -9 "$ppid" 2>/dev/null
                sleep 0.2
                if [ -d "/proc/$zpid" ]; then
                    echo "zombie $zpid still exists after killing parent $ppid" >&2; exit 1
                fi
                echo "zombie $zpid reaped (parent $ppid killed)"
            else
                echo "process $zpid already reaped, no zombie to clean"
            fi
            rm -f "$SIDECAR"
        else
            echo "no active injection for pid=$pid" >&2; exit 1
        fi
        ;;

    query)
        pid=${DCAT_PARAM_PID:?missing required param: pid}
        state=$(awk '/^State:/{print $2}' /proc/$pid/status 2>/dev/null)
        if [ "$state" = "Z" ]; then
            echo "pid=$pid state=Z (zombie)"
            ps -eo pid,ppid,stat,cmd 2>/dev/null | awk -v p="$pid" 'NR==1 || $1==p'
            exit 0
        elif [ -d "/proc/$pid" ]; then
            echo "pid=$pid state=$state (not zombie)"
            exit 1
        else
            echo "pid=$pid not found (already reaped/gone)"
            exit 1
        fi
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2; exit 1
        ;;
esac
