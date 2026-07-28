#!/bin/sh
# rCPU_core_offline: offline CPU cores via sysfs.
# inject: per-core echo 0 > online + touch cN marker (per-core, no overwrite on multi-inject)
# clean:  use record's DCAT_PARAM_CORES → per-core echo 1 > online + rm cN marker
# query:  glob cN markers (or use --cores) → check actual online state
# cpu0 is usually not offlinable; script skips and warns.

parse_cores() {
    echo "$1" | tr ',' '\n' | while IFS= read -r r; do
        [ -z "$r" ] && continue
        case "$r" in
            *-*)
                start=${r%%-*}
                end=${r##*-}
                n=$start
                while [ "$n" -le "$end" ]; do echo "$n"; n=$((n + 1)); done
                ;;
            *)
                echo "$r"
                ;;
        esac
    done
}

case "${DCAT_OP:-inject}" in
    inject)
        spec=${DCAT_PARAM_CORES:?missing required param: cores}
        offlist=""
        for n in $(parse_cores "$spec"); do
            path="/sys/devices/system/cpu/cpu$n/online"
            [ -w "$path" ] || { echo "cpu$n not offlinable (skipping)" >&2; continue; }
            if ! echo 0 > "$path" 2>/dev/null; then
                echo "offline cpu$n failed" >&2; continue
            fi
            touch "/tmp/dcat-rCPU_core_offline-c$n"
            offlist="$offlist $n"
        done
        echo "offlined cores:$offlist"
        ;;

    clean)
        spec="${DCAT_PARAM_CORES:-$(for f in /tmp/dcat-rCPU_core_offline-c*; do [ -f "$f" ] || continue; n=${f##*/dcat-rCPU_core_offline-c}; printf '%s,' "$n"; done)}"
        spec=${spec%,}
        spec=${spec:-0}
        onlist=""
        for n in $(parse_cores "$spec"); do
            path="/sys/devices/system/cpu/cpu$n/online"
            if [ -w "$path" ] && echo 1 > "$path" 2>/dev/null; then
                onlist="$onlist $n"
            fi
            rm -f "/tmp/dcat-rCPU_core_offline-c$n"
        done
        echo "onlined cores:$onlist"
        ;;

    query)
        if [ -n "$DCAT_PARAM_CORES" ]; then
            spec=$DCAT_PARAM_CORES
        else
            spec=""
            for f in /tmp/dcat-rCPU_core_offline-c*; do
                [ -f "$f" ] || continue
                n=${f##*/dcat-rCPU_core_offline-c}
                spec="${spec:+$spec,}$n"
            done
        fi
        spec=${spec:-0}
        any_offline=0
        printf 'core\tonline\tstatus\n'
        for n in $(parse_cores "$spec"); do
            path="/sys/devices/system/cpu/cpu$n/online"
            if [ -r "$path" ]; then
                v=$(cat "$path" 2>/dev/null)
            else
                v="-"
            fi
            if [ "$v" = "0" ]; then
                status="OFFLINE"; any_offline=1
            elif [ "$v" = "1" ]; then
                status="online"
            else
                status="n/a"
            fi
            printf '%s\t%s\t%s\n' "$n" "$v" "$status"
        done
        [ "$any_offline" -eq 1 ]
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2
        exit 1
        ;;
esac
