#!/bin/sh
# rDISK_write_overload: Disk write IO overload (dd writers)
# inject: spawn N dd writers, write pidfile, exit immediately
# clean:  read pidfile, kill dd processes, remove temp files + pidfile, exit
# query:  check dd process count and temp files

dev="${DCAT_PARAM_DEVICE:-}"
dev_clean=$(echo "$dev" | tr '/' '_')
PIDFILE="/tmp/dcat-rDISK_write_overload-${dev_clean}.pid"

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing required param: device}
        workers=${DCAT_PARAM_WORKERS:-4}
        size=${DCAT_PARAM_SIZE_MB:-200}
        dev_clean=$(echo "$dev" | tr '/' '_')
        PIDFILE="/tmp/dcat-rDISK_write_overload-${dev_clean}.pid"

        # determine target path for stress files
        if [ -d "$dev" ]; then
            target="$dev/dcat.stress.$$"
        else
            target="/tmp/dcat.write.$$"
        fi

        pids=""
        i=0
        while [ "$i" -lt "$workers" ]; do
            ( while true; do dd if=/dev/zero of="${target}.${i}" bs=1M count="$size" conv=fdatasync 2>/dev/null || sleep 1; done ) >/dev/null 2>&1 &
            pids="$pids $!"
            i=$((i + 1))
        done
        echo "$pids" > "$PIDFILE"
        echo "injected disk write overload: $workers workers on $dev (pids:$pids)"
        ;;

    clean)
        dev=${DCAT_PARAM_DEVICE:-}
        dev_clean=$(echo "$dev" | tr '/' '_')
        PIDFILE="/tmp/dcat-rDISK_write_overload-${dev_clean}.pid"
        if [ -f "$PIDFILE" ]; then
            for pid in $(cat "$PIDFILE"); do
                kill "$pid" 2>/dev/null
            done
            rm -f "$PIDFILE"
            # remove stress files (pid-prefixed)
            if [ -d "$dev" ]; then
                rm -f "${dev}/dcat.stress."* 2>/dev/null
            fi
            rm -f /tmp/dcat.write.* 2>/dev/null
            echo "cleaned disk write overload on $dev"
        else
            echo "no active injection for device=$dev" >&2
            exit 1
        fi
        ;;

    query)
        dev=${DCAT_PARAM_DEVICE:-/tmp}
        expected=${DCAT_PARAM_WORKERS:-4}
        echo "Checking disk write overload on $dev (expected $expected workers)..."
        dd_procs=$(pgrep -af 'dd if=/dev/zero' 2>/dev/null || true)
        count=$(printf '%s\n' "$dd_procs" | grep -c .)
        if [ "$count" -gt 0 ]; then
            echo "FAULT CONFIRMED: $count dd process(es) running"
            echo "$dd_procs"
            echo "--- temp files ---"
            ls -lh "${dev}"/dcat.stress.* 2>/dev/null || true
            ls -lh /tmp/dcat.write.* 2>/dev/null || true
            exit 0
        else
            echo "FAULT NOT ACTIVE: no dd if=/dev/zero processes found"
            exit 1
        fi
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2
        exit 1
        ;;
esac
