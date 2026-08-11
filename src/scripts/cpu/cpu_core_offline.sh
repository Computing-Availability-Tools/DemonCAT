#!/bin/sh
# rCPU_core_offline: offline CPU cores via sysfs.
# inject: per-core echo 0 > online + touch cN marker (per-core, no overwrite on multi-inject)
# clean:  use record's DCAT_PARAM_CORES â†?per-core echo 1 > online + rm cN marker
# query:  glob cN markers (or use --cores) â†?check actual online state
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
        faillist=""
        for n in $(parse_cores "$spec"); do
            path="/sys/devices/system/cpu/cpu$n/online"
            [ -w "$path" ] || { echo "cpu$n not offlinable (skipping)" >&2; continue; }
            echo 0 > "$path" 2>/dev/null
            actual=$(cat "$path" 2>/dev/null)
            if [ "$actual" = "0" ]; then
                touch "/tmp/dcat-rCPU_core_offline-c$n"
                offlist="$offlist $n"
            else
                echo "offline cpu$n failed (actual=$actual, WSL2 unsupported?)" >&2
                faillist="$faillist $n"
            fi
        done
        echo "offlined cores:$offlist"
        [ -z "$faillist" ] || { echo "failed cores:$faillist" >&2; exit 1; }
        ;;

    clean)
        spec="${DCAT_PARAM_CORES:-$(for f in /tmp/dcat-rCPU_core_offline-c*; do [ -f "$f" ] || continue; n=${f##*/dcat-rCPU_core_offline-c}; printf '%s,' "$n"; done)}"
        spec=${spec%,}
        spec=${spec:-0}
        onlist=""
        faillist=""
        for n in $(parse_cores "$spec"); do
            path="/sys/devices/system/cpu/cpu$n/online"
            if [ -w "$path" ]; then
                echo 1 > "$path" 2>/dev/null
                actual=$(cat "$path" 2>/dev/null)
                if [ "$actual" = "1" ]; then
                    onlist="$onlist $n"
                else
                    faillist="$faillist $n"
                fi
            fi
            rm -f "/tmp/dcat-rCPU_core_offline-c$n"
        done
        echo "onlined cores:$onlist"
        [ -z "$faillist" ] || echo "failed cores:$faillist (WSL2 unsupported?)" >&2
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
