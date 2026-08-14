#!/bin/sh
# rCPU_freq: set CPU scaling_max_freq (underclock) per core.
# inject: clamp scaling_max_freq to freq_mhz on each core; save originals in sidecar
#         if target < scaling_min_freq, lower min first (then restore both on clean)
# clean:  restore original scaling_min_freq and scaling_max_freq per core
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

restore_all() {
    while IFS=' ' read -r rn romax romin; do
        rd="/sys/devices/system/cpu/cpu$rn/cpufreq"
        # Restore max FIRST (raise ceiling), then min (raise floor)
        [ -n "$romax" ] && echo "$romax" > "$rd/scaling_max_freq" 2>/dev/null
        [ -n "$romin" ] && echo "$romin" > "$rd/scaling_min_freq" 2>/dev/null
    done < "$SIDECAR"
    rm -f "$SIDECAR"
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
            [ -d "$d" ] || { echo "cpu$n has no cpufreq sysfs" >&2; rm -f "$SIDECAR"; exit 1; }
            orig_max=$(cat "$d/scaling_max_freq" 2>/dev/null)
            orig_min=$(cat "$d/scaling_min_freq" 2>/dev/null)
            cur_min=${orig_min:-0}

            # If target below scaling_min_freq, lower min first
            if [ "$freq_khz" -lt "$cur_min" ]; then
                if ! echo "$freq_khz" > "$d/scaling_min_freq" 2>/dev/null; then
                    echo "cannot lower scaling_min_freq on cpu$n (min=$((cur_min/1000))MHz, target=${freq}MHz)" >&2
                    rm -f "$SIDECAR"; exit 1
                fi
            fi

            if ! echo "$freq_khz" > "$d/scaling_max_freq" 2>/dev/null; then
                echo "set scaling_max_freq failed on cpu$n" >&2
                restore_all; exit 1
            fi

            actual=$(cat "$d/scaling_max_freq" 2>/dev/null)
            if [ "$actual" != "$freq_khz" ]; then
                echo "freq not applied on cpu$n: wanted=${freq}MHz actual=$((actual/1000))MHz" >&2
                [ -n "$orig_min" ] && echo "$orig_min" > "$d/scaling_min_freq" 2>/dev/null
                echo "$orig_max" > "$d/scaling_max_freq" 2>/dev/null
                restore_all; exit 1
            fi
            printf '%s %s %s\n' "$n" "$orig_max" "$orig_min" >> "$SIDECAR"
        done
        echo "set cpu[$spec] scaling_max_freq=${freq_khz}kHz (${freq}MHz)"
        ;;

    clean)
        [ -s "$SIDECAR" ] || { echo "no active cpu_freq" >&2; exit 1; }
        while read -r n orig_max orig_min; do
            [ -z "$n" ] && continue
            d="/sys/devices/system/cpu/cpu$n/cpufreq"
            # Restore max FIRST (raise ceiling), then min (raise floor)
            [ -n "$orig_max" ] && echo "$orig_max" > "$d/scaling_max_freq" 2>/dev/null || true
            [ -n "$orig_min" ] && echo "$orig_min" > "$d/scaling_min_freq" 2>/dev/null || true
        done < "$SIDECAR"
        rm -f "$SIDECAR"
        echo "cleaned cpu_freq (restored scaling_min/max_freq)"
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
