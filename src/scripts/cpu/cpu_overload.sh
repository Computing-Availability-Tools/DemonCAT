#!/bin/sh
# rCPU_overload: CPU overload (per-core burn with taskset pinning, pure user-space).
# inject: for each specified core, spawn taskset -c <core> perl burn + pidfile, exit
# clean:  read pidfile, kill processes, exit
# query:  check burn process count + per-core CPU usage
#
# cores spec: "0,2,4" or "0-3" or "0-3,7" (same format as rCPU_core_offline)
# load_pct (optional, default 100): target CPU load percentage 1-100.
#   100 = full burn (perl -e '1 while 1'); <100 = duty-cycle loop (work + usleep).
# Falls back to `yes >/dev/null` if perl not available (load_pct ignored, always ~100%).

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

# Variable-load burner: $1 = load_pct (1-100)
# Uses Time::HiRes (core since Perl 5.8) for microsecond duty cycling.
# 10ms period: work for (pct% * 10ms), idle for the rest.

case "${DCAT_OP:-inject}" in
    inject)
        spec=${DCAT_PARAM_CORES:?missing required param: cores}
        load_pct=${DCAT_PARAM_LOAD_PCT:-100}

        # validate cores format: only digits, commas, and hyphens
        case "$spec" in
            *[!0-9,-]*)
                echo "invalid cores spec '$spec': use comma (0,2,4) or range (0-3)" >&2
                exit 1
                ;;
        esac

        # validate load_pct: 1-100
        if ! echo "$load_pct" | grep -qE '^[0-9]+$' 2>/dev/null; then
            echo "load_pct must be a number (1-100), got: '$load_pct'" >&2
            exit 1
        fi
        if [ "$load_pct" -lt 1 ] 2>/dev/null || [ "$load_pct" -gt 100 ] 2>/dev/null; then
            echo "load_pct must be 1-100, got: $load_pct" >&2
            exit 1
        fi

        pids=""
        for n in $(parse_cores "$spec"); do
            CORE_PF="/tmp/dcat-rCPU_overload-c${n}.pid"
            if [ -f "$CORE_PF" ]; then
                kill "$(cat "$CORE_PF" 2>/dev/null)" 2>/dev/null
                rm -f "$CORE_PF"
            fi
            if command -v perl >/dev/null 2>&1; then
                if [ "$load_pct" -ge 100 ] 2>/dev/null; then
                    taskset -c "$n" perl -e '1 while 1' >/dev/null 2>&1 &
                else
                    taskset -c "$n" perl -e '
use Time::HiRes qw(usleep gettimeofday);
my $pct=shift||100; $pct=100 if $pct>100; $pct=1 if $pct<1;
my $period=10000; my $work=int($period*$pct/100); my $idle=$period-$work;
while(1){ my $s=gettimeofday(); while((gettimeofday()-$s)*1e6<$work){1} usleep($idle) }
' "$load_pct" >/dev/null 2>&1 &
                fi
            else
                taskset -c "$n" yes >/dev/null 2>&1 &
            fi
            echo $! > "$CORE_PF"
            pids="$pids $!"
        done
        echo "injected CPU overload on cores [$spec] load=${load_pct}% (pids:$pids)"
        ;;

    clean)
        spec="${DCAT_PARAM_CORES:-}"
        any=0
        for n in $(parse_cores "$spec"); do
            CORE_PF="/tmp/dcat-rCPU_overload-c${n}.pid"
            if [ -f "$CORE_PF" ]; then
                kill "$(cat "$CORE_PF" 2>/dev/null)" 2>/dev/null
                rm -f "$CORE_PF"
                any=1
            fi
        done
        if [ "$any" = 1 ]; then
            echo "cleaned CPU overload on cores [$spec]"
        else
            echo "cleaned CPU overload on cores [$spec] (no active injection)"
        fi
        ;;

    query)
        spec=${DCAT_PARAM_CORES:-0}
        burn_count=$(pgrep -f 'perl -e' 2>/dev/null | wc -l)
        yes_count=$(pgrep -x yes 2>/dev/null | wc -l)
        burn_count=${burn_count## }
        yes_count=${yes_count## }
        total=$((burn_count + yes_count))
        echo "requested_cores: $spec"
        echo "burn_processes: $total"
        echo "--- per-core CPU (requested only) ---"
        if command -v mpstat >/dev/null 2>&1; then
            first=1
            for n in $(parse_cores "$spec"); do
                if [ "$first" = 1 ]; then
                    mpstat -P "$n" 1 1 2>/dev/null | tail -2
                    first=0
                else
                    mpstat -P "$n" 1 1 2>/dev/null | tail -1
                fi
            done
        else
            echo "(mpstat unavailable — apt install sysstat for per-core view)"
        fi
        echo "--- burn process details ---"
        ps -eo pid,%cpu,psr,cmd 2>/dev/null | grep -E 'PID|[p]erl|[y]es' || echo "(none)"
        [ "$total" -gt 0 ]
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2
        exit 1
        ;;
esac
