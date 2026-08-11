#!/bin/sh
# rPROC_zstate: turn a real process into a zombie by killing it.
# inject: kill -9 $pid â†?process exits â†?zombie if parent doesn't reap
# clean:  kill the parent â†?zombie reparented to init â†?init reaps
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
            echo "process $pid killed â†?zombie (parent $ppid not reaping)"
        else
            echo "process $pid killed (parent $ppid reaped immediately, no zombie persisted)"
        fi
        ;;

    clean)
        if [ -n "$DCAT_PARAM_PID" ]; then
            pids="$DCAT_PARAM_PID"
        else
            pids=""
            for sc in /tmp/dcat-rPROC_zstate-*.info; do
                [ -f "$sc" ] || continue
                read -r zpid _ < "$sc" 2>/dev/null
                [ -n "$zpid" ] && pids="$pids $zpid"
            done
        fi
        cleaned=0
        for pid in $pids; do
            [ -n "$pid" ] || continue
            SIDECAR="/tmp/dcat-rPROC_zstate-$pid.info"
            [ -f "$SIDECAR" ] || continue
            read -r zpid ppid < "$SIDECAR"
            state=$(awk '/^State:/{print $2}' /proc/$zpid/status 2>/dev/null)
            if [ "$state" = "Z" ]; then
                kill -9 "$ppid" 2>/dev/null
                sleep 0.2
                if [ -d "/proc/$zpid" ]; then
                    echo "zombie $zpid still exists after killing parent $ppid" >&2; exit 1
                fi
                cleaned=1
            fi
            rm -f "$SIDECAR"
        done
        if [ "$cleaned" = 1 ]; then echo "reaped zombies [$pids]";
        else echo "reaped zombies (no active injection)"; fi
        ;;

    query)
        pids=""
        if [ -n "$DCAT_PARAM_PID" ]; then
            pids=$DCAT_PARAM_PID
        else
            for sc in /tmp/dcat-rPROC_zstate-*.info; do
                [ -f "$sc" ] || continue
                read -r zpid _ < "$sc" 2>/dev/null
                [ -n "$zpid" ] && pids="$pids $zpid"
            done
        fi
        [ -z "$pids" ] && { echo "no pid (no active rPROC_zstate injection)"; exit 1; }
        found=0
        for pid in $pids; do
            [ -n "$pid" ] || continue
            state=$(awk '/^State:/{print $2}' "/proc/$pid/status" 2>/dev/null)
            if [ "$state" = "Z" ]; then
                echo "pid=$pid state=Z (zombie)"
                ps -eo pid,ppid,stat,cmd 2>/dev/null | awk -v p="$pid" 'NR==1 || $1==p'
                found=1
            elif [ -d "/proc/$pid" ]; then
                echo "pid=$pid state=$state (not zombie)"
            else
                echo "pid=$pid not found (already reaped/gone)"
            fi
        done
        [ "$found" = 1 ] && exit 0 || exit 1
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2; exit 1
        ;;
esac
