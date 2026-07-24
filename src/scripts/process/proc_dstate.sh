#!/bin/sh
# rPROC_dstate: create processes in D (uninterruptible sleep) state via real I/O.
# inject: spawn dd processes doing fdatasync on a real block device → D state during fsync
# clean:  kill the dd processes
# query:  check for D state processes
# Note: D state only occurs on real block devices (not tmpfs). On tmpfs, I/O completes
# instantly and no D state is produced. Use a real disk (e.g. /dev/sda or a mounted ext4 dir).

case "${DCAT_OP:-inject}" in
    inject)
        device=${DCAT_PARAM_DEVICE:?missing required param: device}
        if [ ! -e "$device" ]; then
            echo "device '$device' not found" >&2; exit 1
        fi
        PIDFILE="/tmp/dcat-rPROC_dstate-$(echo "$device" | tr '/' '_').pid"
        pids=""
        i=0
        while [ "$i" -lt 2 ]; do
            (
                trap 'kill $! 2>/dev/null; exit 0' TERM
                while true; do
                    dd if=/dev/zero of="${device}/dcat.dstate.$$.$i" bs=1M count=200 conv=fdatasync 2>/dev/null &
                    wait $!
                done
            ) >/dev/null 2>&1 &
            pids="$pids $!"
            i=$((i + 1))
        done
        echo "$pids" > "$PIDFILE"
        echo "injected D-state workers on $device (pids:$pids)"
        ;;

    clean)
        device="${DCAT_PARAM_DEVICE:-}"
        PIDFILE="/tmp/dcat-rPROC_dstate-$(echo "$device" | tr '/' '_').pid"
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE"); do
                kill "$pid" 2>/dev/null
            done
            rm -f "$PIDFILE"
        fi
        rm -f "${device}"/dcat.dstate.* 2>/dev/null
        rm -f /tmp/dcat.dstate.* 2>/dev/null
        echo "cleaned D-state workers"
        ;;

    query)
        list=$(ps -eo pid,stat,cmd 2>/dev/null | awk '$2 ~ /^D/')
        if [ -n "$list" ]; then
            n=$(printf '%s\n' "$list" | awk 'END{print NR}')
            echo "dstate_count=$n"
            printf 'PID\tSTAT\tCMD\n'
            printf '%s\n' "$list"
            exit 0
        else
            echo "dstate_count=0"
            echo "(D state requires real block device; tmpfs I/O completes instantly)"
            exit 1
        fi
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2; exit 1
        ;;
esac
