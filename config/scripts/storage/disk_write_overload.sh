#!/bin/sh
# rDISK_write_overload: spawn N dd writers (background).
# Env: DCAT_OP, DCAT_PARAM_DEVICE (path or block dev),
#      DCAT_PARAM_WORKERS (default 4), DCAT_PARAM_SIZE_MB (per-worker file size, default 200)
# Files are pid-prefixed (/tmp/dcat.stress.<pid>.<i>) so concurrent injects don't collide;
# cleanup tracks child pids and kills them explicitly (not kill 0, which would kill the
# script before rm runs).
children=""
cleanup() {
    trap - TERM INT
    # kill tracked dd subshells explicitly (NOT 'kill 0' — that kills the script
    # itself before rm runs, leaving temp files orphaned)
    for c in $children; do kill "$c" 2>/dev/null; done
    # remove this inject's files (pid-prefixed -> only its own)
    dev="${DCAT_PARAM_DEVICE:-}"
    if [ -n "$dev" ] && [ -d "$dev" ]; then
        rm -f "${dev}/dcat.stress.$$."* 2>/dev/null
    else
        rm -f /tmp/dcat.write.$$.* 2>/dev/null
    fi
    exit 0
}
trap cleanup TERM INT

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing device}
        workers=${DCAT_PARAM_WORKERS:-4}
        size=${DCAT_PARAM_SIZE_MB:-200}
        # pid-prefixed target so concurrent injects don't collide on the same filename
        if [ -d "$dev" ]; then
            target="$dev/dcat.stress.$$"
        else
            target="/tmp/dcat.write.$$"
        fi
        i=0
        while [ "$i" -lt "$workers" ]; do
            ( while true; do dd if=/dev/zero of="${target}.${i}" bs=1M count="$size" conv=fdatasync 2>/dev/null || sleep 1; done ) &
            children="$children $!"
            i=$((i + 1))
        done
        # stay alive so dcat can always deliver SIGTERM -> cleanup runs
        while true; do sleep 3600; done
        ;;
    clean)
        cleanup
        ;;
    query)
        dev=${DCAT_PARAM_DEVICE:-/tmp}
        expected=${DCAT_PARAM_WORKERS:-4}
        echo "Checking disk write overload on $dev (expected $expected workers)..."
        dd_procs=$(pgrep -af 'dd if=/dev/zero' 2>/dev/null || true)
        count=$(printf '%s\n' "$dd_procs" | grep -c .)
        if [ "$count" -gt 0 ]; then
            echo "FAULT CONFIRMED: $count dd process(es) running (expected $expected)"
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
esac
