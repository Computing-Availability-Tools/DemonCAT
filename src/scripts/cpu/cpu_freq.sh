#!/bin/sh
# rCPU_freq: set CPU scaling_max_freq (underclock) per core.
# inject: clamp scaling_max_freq to freq_mhz on each core; save originals in sidecar
# clean:  restore original scaling_max_freq per core
# query:  show current scaling_max_freq

SIDECAR="/tmp/dcat-rCPU_freq.sidecar"

parse_cores() {
    echo "$1" | tr ',' '\n' | while IFS= read -r r; do
        [ -z "$r" ] && continue
        case "$r" in
            *-*)
                start=${r%%-*}; end=${r##*-}; n=$start
                while [ "$n" -le "$end" ]; do echo "$n"; n=$((n + 1)); done
                ;;
            *) echo "$r" ;;
        esac
    done
}

case "${DCAT_OP:-inject}" in
    inject)
        spec=${DCAT_PARAM_CORES:?missing required param: cores}
        freq=${DCAT_PARAM_FREQ_MHZ:?missing required param: freq_mhz}
        case "$freq" in
            *[!0-9]*|"") echo "freq_mhz must be a positive integer, got: '$freq'" >&2; exit 1;;
        esac
        freq_khz=$((freq * 1000))
        : > "$SIDECAR"
        for n in $(parse_cores "$spec"); do
            d="/sys/devices/system/cpu/cpu$n/cpufreq"
            [ -d "$d" ] || { echo "cpu$n has no cpufreq sysfs" >&2; exit 1; }
            orig=$(cat "$d/scaling_max_freq" 2>/dev/null)
            printf '%s %s\n' "$n" "$orig" >> "$SIDECAR"
            echo "$freq_khz" > "$d/scaling_max_freq" 2>/dev/null || { echo "set scaling_max_freq failed on cpu$n (need root?)" >&2; exit 1; }
        done
        echo "set cpu[$spec] scaling_max_freq=${freq_khz}kHz (${freq}MHz)"
        ;;

    clean)
        [ -s "$SIDECAR" ] || { echo "no active cpu_freq" >&2; exit 1; }
        while read -r n orig; do
            [ -z "$n" ] && continue
            d="/sys/devices/system/cpu/cpu$n/cpufreq"
            [ -n "$orig" ] && echo "$orig" > "$d/scaling_max_freq" 2>/dev/null || true
        done < "$SIDECAR"
        rm -f "$SIDECAR"
        echo "cleaned cpu_freq (restored scaling_max_freq)"
        ;;

    query)
        spec=${DCAT_PARAM_CORES:-0}
        echo "cpu[$spec] scaling_max_freq (kHz):"
        for n in $(parse_cores "$spec"); do
            d="/sys/devices/system/cpu/cpu$n/cpufreq"
            if [ -d "$d" ]; then
                cur=$(cat "$d/scaling_max_freq" 2>/dev/null)
                echo "  cpu$n max=$cur"
            fi
        done
        [ -s "$SIDECAR" ] && exit 0 || exit 1
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
