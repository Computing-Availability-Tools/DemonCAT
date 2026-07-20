#!/bin/sh
# rPROC_dstate: spawn N processes doing uninterruptible IO (best-effort D-state).
# Env: DCAT_OP, DCAT_PARAM_COUNT
# Note: true D-state needs a stalled device/kernel module. v0.2 uses best-effort
# large sync writes (fsync blocks in D state briefly). Background; trap SIGTERM.
children=""
cleanup() {
    trap - TERM INT
    # kill tracked dd subshells explicitly (not 'kill 0' — that kills the script
    # before rm runs, leaving temp files orphaned)
    for c in $children; do kill "$c" 2>/dev/null; done
    rm -f /tmp/dcat.dstate.* 2>/dev/null
    exit 0
}
trap cleanup TERM INT

case "${DCAT_OP:-inject}" in
    inject)
        count=${DCAT_PARAM_COUNT:?missing count}
        tmpf=/tmp/dcat.dstate.$$
        i=0
        while [ "$i" -lt "$count" ]; do
            # large writes + fsync enter D state during sync
            ( while true; do dd if=/dev/zero of="$tmpf.$i" bs=1M count=100 conv=fdatasync 2>/dev/null || sleep 1; done ) &
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
        count=${DCAT_PARAM_COUNT:-1}
        echo "expected_count=$count"
        list=$(ps -eo pid,stat,cmd 2>/dev/null | awk '$2 ~ /^D/')
        if [ -n "$list" ]; then
            n=$(printf '%s\n' "$list" | awk 'END{print NR}')
            echo "dstate_count=$n"
            printf 'PID\tSTAT\tCMD\n'
            printf '%s\n' "$list"
            exit 0
        fi
        echo "dstate_count=0"
        exit 1
        ;;
esac
