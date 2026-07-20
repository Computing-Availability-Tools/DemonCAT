#!/bin/sh
# rCPU_overload: CPU overload (multi-core burn, background mode).
# Env: DCAT_OP, DCAT_PARAM_CORES (number of cores to burn)
# Duration is handled by dcat (state_add + reaper); script just stays alive
# until dcat delivers SIGTERM -> cleanup kills tracked children.
children=""
cleanup() {
    trap - TERM INT
    # kill tracked children explicitly (NOT 'kill 0' — that kills the script
    # itself before cleanup can complete, same fix as disk_write_overload.sh)
    for c in $children; do kill "$c" 2>/dev/null; done
    exit 0
}
trap cleanup TERM INT

case "${DCAT_OP:-inject}" in
    inject)
        cores=${DCAT_PARAM_CORES:?missing cores}
        i=0
        while [ "$i" -lt "$cores" ]; do
            yes >/dev/null 2>&1 &
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
        cores=${DCAT_PARAM_CORES:-1}
        yes_count=$(pgrep -x yes 2>/dev/null | wc -l)
        yes_count=${yes_count## }
        echo "requested_cores: $cores"
        echo "yes_processes: $yes_count"
        echo "--- cpu usage ---"
        if command -v mpstat >/dev/null 2>&1; then
            mpstat 1 1 2>/dev/null | tail -5
        else
            top -bn1 2>/dev/null | head -5
        fi
        [ "$yes_count" -gt 0 ]
        ;;
esac
