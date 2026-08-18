#!/bin/sh
# rCPU_freq: set CPU scaling_max_freq (underclock) per core.
# inject: clamp scaling_max_freq to freq_mhz on each core; save originals in sidecar
#         if target < scaling_min_freq, lower min first (then restore both on clean)
# clean:  restore original scaling_min_freq and scaling_max_freq per core
# query:  show current scaling_max_freq

SIDECAR_PFX="/tmp/dcat-rCPU_freq"

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
        safe=$(echo "$spec" | tr -c 'a-zA-Z0-9' '_')
        SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
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
        clean_freq_file() {
            [ -s "$1" ] || return 1
            while read -r n orig_max orig_min; do
                [ -z "$n" ] && continue
                d="/sys/devices/system/cpu/cpu$n/cpufreq"
                # Restore max FIRST (raise ceiling), then min (raise floor)
                [ -n "$orig_max" ] && echo "$orig_max" > "$d/scaling_max_freq" 2>/dev/null || true
                [ -n "$orig_min" ] && echo "$orig_min" > "$d/scaling_min_freq" 2>/dev/null || true
            done < "$1"
            rm -f "$1"
        }
        if [ -n "${DCAT_PARAM_CORES:-}" ]; then
            safe=$(echo "$DCAT_PARAM_CORES" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            clean_freq_file "$SIDECAR" || { echo "no active cpu_freq" >&2; exit 0; }
            echo "cleaned cpu_freq (restored scaling_min/max_freq)"
        else
            found=0
            for f in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$f" ] || continue
                clean_freq_file "$f" && found=1
            done
            if [ "$found" -eq 0 ]; then
                echo "no active cpu_freq" >&2
                exit 0
            fi
            echo "cleaned all cpu_freq"
        fi
        ;;

    query)
        spec=${DCAT_PARAM_CORES:-}
        active=0
        if [ -n "$spec" ]; then
            safe=$(echo "$spec" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            [ -s "$SIDECAR" ] && active=1
        else
            spec=""
            for f in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$f" ] || continue
                [ -s "$f" ] || continue
                active=1
                cores_in_file=$(awk '{printf "%s,", $1}' "$f" | sed 's/,$//')
                [ -n "$cores_in_file" ] && spec="${spec:+$spec,}$cores_in_file"
            done
            [ -z "$spec" ] && spec="0"
        fi
        echo "cpu[$spec] scaling_max_freq (kHz):"
        for n in $(parse_cores "$spec"); do
            d="/sys/devices/system/cpu/cpu$n/cpufreq"
            if [ -d "$d" ]; then
                cur=$(cat "$d/scaling_max_freq" 2>/dev/null)
                echo "  cpu$n max=$cur"
            fi
        done
        [ "$active" -eq 1 ] && exit 0 || exit 1
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
