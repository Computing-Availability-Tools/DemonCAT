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
            pid=$!
            sleep 0.05
            if kill -0 "$pid" 2>/dev/null; then
                echo "$pid" > "$CORE_PF"
                pids="$pids $pid"
            else
                echo "WARNING: core $n inject failed (core not available? cpuset restricted?)" >&2
                rm -f "$CORE_PF"
            fi
        done
        echo "injected CPU overload on cores [$spec] load=${load_pct}% (pids:$pids)"
        ;;

    clean)
        spec="${DCAT_PARAM_CORES:-}"
        if [ -z "$spec" ]; then
            spec=""
            for pf in /tmp/dcat-rCPU_overload-c*.pid; do
                [ -f "$pf" ] || continue
                n=${pf##*/dcat-rCPU_overload-c}; n=${n%.pid}
                spec="${spec:+$spec,}$n"
            done
        fi
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
        # 有 cores 参数 → 按指定核; 无参数 → 从 per-core pidfile 探测实际注入的核 (权威, 避免 ps/grep 误匹配)
        if [ -n "$DCAT_PARAM_CORES" ]; then
            spec=$DCAT_PARAM_CORES
            echo "requested_cores: $spec"
        else
            spec=$(for pf in /tmp/dcat-rCPU_overload-c*.pid; do
                [ -f "$pf" ] || continue
                pid=$(cat "$pf" 2>/dev/null)
                [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || continue
                n=${pf##*/dcat-rCPU_overload-c}; n=${n%.pid}
                echo "$n"
            done | sort -n | tr '\n' ',' | sed 's/,$//')
            echo "injected_cores: ${spec:-(none)}"
        fi
        # burn 进程数 (按 spec 指定核统计存活)
        total=0
        for n in $(parse_cores "$spec"); do
            CORE_PF="/tmp/dcat-rCPU_overload-c${n}.pid"
            [ -f "$CORE_PF" ] || continue
            pid=$(cat "$CORE_PF" 2>/dev/null)
            [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null && total=$((total+1))
        done
        echo "burn_processes: $total"
        if [ "$total" -gt 0 ]; then
            echo "--- per-core CPU (instantaneous, /proc/stat) ---"
            # 采样 /proc/stat 两次算 delta → 每核 %us (无外部依赖; 与 top/mpstat 同算法, 瞬时值非生命周期均值)
            s1=$(awk '/^cpu[0-9]/{sub(/^cpu/,"",$1); print $1,$2,$3,$4,$5,$6,$7,$8,$9}' /proc/stat 2>/dev/null)
            sleep 0.2
            s2=$(awk '/^cpu[0-9]/{sub(/^cpu/,"",$1); print $1,$2,$3,$4,$5,$6,$7,$8,$9}' /proc/stat 2>/dev/null)
            req=$(parse_cores "$spec" | tr '\n' ' ')
            printf '%s\n' "$s1" | awk -v s2="$s2" -v req="$req" '
            BEGIN { n=split(req,a," "); for(i=1;i<=n;i++) want[a[i]]=1 }
            { c=$1; uu1[c]=$2+$3; tt1[c]=$2+$3+$4+$5+$6+$7+$8+$9 }
            END {
                ns=split(s2,ln,"\n")
                for(i=1;i<=ns;i++){ split(ln[i],f); c=f[1]; if(!(c in want)) continue
                    u2=f[2]+f[3]; t2=f[2]+f[3]+f[4]+f[5]+f[6]+f[7]+f[8]+f[9]
                    dt=t2-tt1[c]; if(dt<=0) dt=1
                    printf "core %-3s: %5.1f%% us\n", c, (u2-uu1[c])/dt*100
                }
            }'
            echo "--- burn process details ---"
            echo "    PID  PSR CMD"
            for n in $(parse_cores "$spec"); do
                CORE_PF="/tmp/dcat-rCPU_overload-c${n}.pid"
                [ -f "$CORE_PF" ] || continue
                pid=$(cat "$CORE_PF" 2>/dev/null)
                [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || continue
                ps -p "$pid" -o pid=,psr=,cmd= 2>/dev/null
            done
        else
            echo "(no active rCPU_overload injection — run 'dcat inject rCPU_overload --cores=...')"
        fi
        [ "$total" -gt 0 ]
        ;;

    *)
        echo "unknown op: $DCAT_OP" >&2
        exit 1
        ;;
esac
