#!/bin/sh
# rDISK_write_overload: Disk write IO overload (dd writers)
# inject: spawn N dd writers (infinite), write pidfile, exit immediately
# clean:  kill dd writers via pkill + pidfile, remove temp files, exit
# query:  check dd process count and temp files

dev="${DCAT_PARAM_DEVICE:-}"
dev_clean=$(echo "$dev" | tr '/' '_')
PIDFILE="/tmp/dcat-rDISK_write_overload-${dev_clean}.pid"

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing required param: device}
        workers=${DCAT_PARAM_WORKERS:-4}
        size=${DCAT_PARAM_SIZE_MB:-200}

        case "$workers" in ''|*[!0-9]*) echo "workers must be a positive integer, got: '$workers'" >&2; exit 1 ;; esac
        [ "$workers" -ge 1 ] 2>/dev/null || { echo "workers must be >= 1, got: $workers" >&2; exit 1; }
        case "$size" in ''|*[!0-9]*) echo "size_mb must be a positive integer, got: '$size'" >&2; exit 1 ;; esac
        [ "$size" -ge 1 ] 2>/dev/null || { echo "size_mb must be >= 1, got: $size" >&2; exit 1; }

        case "$dev" in
            /*) ;;
            *) echo "device path must be absolute, got: '$dev'" >&2; exit 1 ;;
        esac
        [ -d "$dev" ] || { echo "device path not found: $dev" >&2; exit 1; }
        if [ -L "$dev" ]; then
            real=$(readlink -f "$dev" 2>/dev/null || echo "$dev")
            echo "device path must not be a symlink: '$dev' -> '$real'" >&2; exit 1
        fi

        dev_clean=$(echo "$dev" | tr '/' '_')
        PIDFILE="/tmp/dcat-rDISK_write_overload-${dev_clean}.pid"

        if [ -f "$PIDFILE" ]; then
            old_pids=$(sed -n '1p' "$PIDFILE" 2>/dev/null)
            for pid in $old_pids; do kill -9 "$pid" 2>/dev/null; done
            rm -f "$PIDFILE"
        fi
        pkill -9 -f 'dd.*dcat\.stress' 2>/dev/null
        pkill -9 -f 'dd.*dcat\.write' 2>/dev/null
        rm -f /tmp/dcat.stress.* /tmp/dcat.write.* 2>/dev/null
        sleep 0.2
        pkill -9 -f 'dd.*dcat\.stress' 2>/dev/null

        target="$dev/dcat.stress.$$"
        pids=""
        i=0
        while [ "$i" -lt "$workers" ]; do
            dd if=/dev/zero of="${target}.${i}" bs=1M 2>/dev/null &
            pids="$pids $!"
            i=$((i + 1))
        done
        { echo "$pids"; echo "$dev"; } > "$PIDFILE"
        echo "injected disk write overload: $workers workers on $dev (pids:$pids)"
        ;;

    clean)
        if [ -n "$DCAT_PARAM_DEVICE" ]; then
            dev_cleans=$(echo "$DCAT_PARAM_DEVICE" | tr '/' '_')
        else
            dev_cleans=""
            for pf in /tmp/dcat-rDISK_write_overload-*.pid; do
                [ -f "$pf" ] || continue
                d=${pf##*/dcat-rDISK_write_overload-}; d=${d%.pid}
                dev_cleans="$dev_cleans $d"
            done
        fi
        cleaned=0
        for dev_clean in $dev_cleans; do
            [ -n "$dev_clean" ] || continue
            PIDFILE="/tmp/dcat-rDISK_write_overload-${dev_clean}.pid"
            if [ -f "$PIDFILE" ]; then
                pids=$(sed -n '1p' "$PIDFILE" 2>/dev/null)
                dev=$(sed -n '2p' "$PIDFILE" 2>/dev/null)
                for pid in $pids; do
                    kill -9 "$pid" 2>/dev/null
                done
                rm -f "$PIDFILE"
                [ -n "$dev" ] && rm -f "${dev}"/dcat.stress.* 2>/dev/null
                cleaned=1
            fi
        done
        pkill -9 -f 'dd.*dcat\.stress' 2>/dev/null
        pkill -9 -f 'dd.*dcat\.write' 2>/dev/null
        rm -f /tmp/dcat.write.* /tmp/dcat.stress.* 2>/dev/null
        rm -f /etc/dcat.stress.* /etc/dcat.write.* 2>/dev/null
        sleep 0.5
        pkill -9 -f 'dd.*dcat\.stress' 2>/dev/null
        pkill -9 -f 'dd.*dcat\.write' 2>/dev/null
        rm -f /tmp/dcat.stress.* /tmp/dcat.write.* 2>/dev/null
        if [ "$cleaned" = 1 ]; then echo "cleaned";
        else echo "cleaned (no active injection)"; fi
        ;;

    query)
        dev=${DCAT_PARAM_DEVICE:-/tmp}
        echo "Checking disk write overload on $dev..."
        dd_procs=$(pgrep -af 'dd.*dcat.stress' 2>/dev/null; pgrep -af 'dd.*dcat.write' 2>/dev/null || true)
        dd_procs=$(echo "$dd_procs" | grep -v '^$' || true)
        if [ -n "$dd_procs" ]; then
            count=$(echo "$dd_procs" | grep -c . 2>/dev/null)
            count=${count:-0}
        else
            count=0
        fi
        if [ "$count" -gt 0 ]; then
            echo "FAULT CONFIRMED: $count dd process(es) running"
            echo "$dd_procs"
            echo "--- temp files ---"
            ls -lh "${dev}"/dcat.stress.* 2>/dev/null || true
            ls -lh /tmp/dcat.write.* 2>/dev/null || true
            exit 0
        else
            echo "FAULT NOT ACTIVE: no dcat disk write processes found"
            exit 1
        fi
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
