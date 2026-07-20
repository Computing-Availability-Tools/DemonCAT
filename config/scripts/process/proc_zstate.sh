#!/bin/sh
# rPROC_zstate: spawn N zombie children (fork+exit without wait). Background.
# Env: DCAT_OP, DCAT_PARAM_COUNT
# Each child exits immediately; parent (this script) never waits -> children stay Z.
cleanup() { trap - TERM INT; kill 0 2>/dev/null; exit 0; }
trap cleanup TERM INT

case "${DCAT_OP:-inject}" in
    inject)
        count=${DCAT_PARAM_COUNT:?missing count}
        i=0
        while [ "$i" -lt "$count" ]; do
            # child: exit immediately; parent doesn't wait -> zombie
            ( : ) &
            i=$((i + 1))
        done
        # parent stays alive holding the zombies; wait for SIGTERM
        while true; do sleep 3600; done
        ;;
    clean)
        cleanup
        ;;
    query)
        count=${DCAT_PARAM_COUNT:-1}
        echo "expected_count=$count"
        list=$(ps -eo pid,stat,cmd 2>/dev/null | awk '$2 ~ /^Z/')
        if [ -n "$list" ]; then
            n=$(printf '%s\n' "$list" | awk 'END{print NR}')
            echo "zombie_count=$n"
            printf 'PID\tSTAT\tCMD\n'
            printf '%s\n' "$list"
            exit 0
        fi
        echo "zombie_count=0"
        exit 1
        ;;
esac
