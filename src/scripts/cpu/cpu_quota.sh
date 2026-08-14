#!/bin/sh
# rCPU_quota: limit CPU core max utilization (1-99%) via cgroup.
# inject: create cgroup, set cpuset.cpus=<cores> + cpu.max=<quota>, move target core's PIDs into it
# clean:  move PIDs back to root cgroup, restore, remove cgroup
# query:  show current quota, cpuset, and PIDs in the cgroup

SIDECAR="/tmp/dcat-rCPU_quota.sidecar"
PIDLIST="/tmp/dcat-rCPU_quota.pids"

detect_cg_version() {
    [ -f /sys/fs/cgroup/cgroup.controllers ] && echo 2 || echo 1
}

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

# Find movable PIDs currently running on target cores via /proc/PID/stat field 39
find_pids_on_cores() {
    core_list=$(parse_cores "$1" | tr '\n' ' ')
    for d in /proc/[0-9]*; do
        pid=${d##*/}
        [ "$pid" -lt 100 ] 2>/dev/null && continue
        [ "$pid" = "$$" ] && continue
        [ "$pid" = "$PPID" ] && continue
        cpu=$(awk '{print $39}' "$d/stat" 2>/dev/null)
        for n in $core_list; do
            [ "$cpu" = "$n" ] && echo "$pid" && break
        done
    done 2>/dev/null | sort -un
}

case "${DCAT_OP:-inject}" in
    inject)
        cores=${DCAT_PARAM_CORES:?missing required param: cores}
        quota=${DCAT_PARAM_QUOTA_PCT:?missing required param: quota_pct}
        case "$quota" in
            *[!0-9]*|"") echo "quota_pct must be an integer 1-99, got: '$quota'" >&2; exit 1;;
        esac
        if [ "$quota" -lt 1 ] || [ "$quota" -gt 99 ]; then
            echo "quota_pct must be 1-99, got: $quota" >&2; exit 1
        fi
        ver=$(detect_cg_version)
        period=100000
        quota_us=$(( quota * period / 100 ))
        cpus=$(parse_cores "$cores" | tr '\n' ',' | sed 's/,$//')

        if [ "$ver" = 2 ]; then
            base="/sys/fs/cgroup/dcat_quota"
            mkdir -p "$base" 2>/dev/null || { echo "cannot mkdir $base (need root?)" >&2; exit 1; }
            echo "+cpu +cpuset" > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
            echo "$cpus" > "$base/cpuset.cpus" 2>/dev/null || true
            orig_max=$(cat "$base/cpu.max" 2>/dev/null)
            echo "$quota_us $period" > "$base/cpu.max" 2>/dev/null || { echo "set cpu.max failed" >&2; exit 1; }
            : > "$PIDLIST"
            echo "--- moving PIDs from cores [$cpus] into $base ---"
            moved=0
            for pid in $(find_pids_on_cores "$cores"); do
                if echo "$pid" > "$base/cgroup.procs" 2>/dev/null; then
                    echo "$pid" >> "$PIDLIST"; moved=$((moved + 1))
                fi
            done
            printf '%s\n2\n%s\n%s\n' "$base" "$orig_max" "$cpus" > "$SIDECAR"
            echo "set cpu.max=$quota_us $period, cpuset.cpus=$cpus on $base (moved $moved PIDs)"
        else
            base="/sys/fs/cgroup/cpu/dcat_quota"
            cbase="/sys/fs/cgroup/cpuset/dcat_quota"
            mkdir -p "$base" 2>/dev/null || { echo "cannot mkdir $base (need root?)" >&2; exit 1; }
            mkdir -p "$cbase" 2>/dev/null || true
            orig_q=$(cat "$base/cpu.cfs_quota_us" 2>/dev/null)
            echo "$quota_us" > "$base/cpu.cfs_quota_us" 2>/dev/null || { echo "set cfs_quota_us failed" >&2; exit 1; }
            echo "100000" > "$base/cpu.cfs_period_us" 2>/dev/null || true
            echo "$cpus" > "$cbase/cpuset.cpus" 2>/dev/null || true
            echo "0" > "$cbase/cpuset.mems" 2>/dev/null || true
            : > "$PIDLIST"
            echo "--- moving PIDs from cores [$cpus] into $base + $cbase ---"
            moved=0
            for pid in $(find_pids_on_cores "$cores"); do
                echo "$pid" > "$cbase/tasks" 2>/dev/null || true
                if echo "$pid" > "$base/tasks" 2>/dev/null; then
                    echo "$pid" >> "$PIDLIST"; moved=$((moved + 1))
                fi
            done
            printf '%s\n1\n%s\n%s\n' "$base" "$orig_q" "$cbase" > "$SIDECAR"
            echo "set cfs_quota_us=$quota_us on $base, cpuset=$cpus on $cbase (moved $moved PIDs)"
        fi
        ;;

    clean)
        [ -f "$SIDECAR" ] || { echo "no active cpu_quota" >&2; exit 1; }
        { read -r base; read -r ver; read -r orig; read -r extra; } < "$SIDECAR"
        if [ "$ver" = 2 ]; then
            if [ -f "$base/cgroup.procs" ]; then
                while read -r p; do
                    [ -n "$p" ] && echo "$p" > /sys/fs/cgroup/cgroup.procs 2>/dev/null
                done < "$base/cgroup.procs"
            fi
            echo "${orig:-max}" > "$base/cpu.max" 2>/dev/null || true
            rmdir "$base" 2>/dev/null || true
        else
            cbase="$extra"
            if [ -f "$base/tasks" ]; then
                while read -r p; do
                    [ -n "$p" ] && echo "$p" > /sys/fs/cgroup/cpu/tasks 2>/dev/null
                done < "$base/tasks"
            fi
            if [ -f "$cbase/tasks" ]; then
                while read -r p; do
                    [ -n "$p" ] && echo "$p" > /sys/fs/cgroup/cpuset/tasks 2>/dev/null
                done < "$cbase/tasks"
            fi
            echo "${orig:--1}" > "$base/cpu.cfs_quota_us" 2>/dev/null || true
            rmdir "$base" 2>/dev/null || true
            rmdir "$cbase" 2>/dev/null || true
        fi
        rm -f "$SIDECAR" "$PIDLIST"
        echo "cleaned cpu_quota (restored, moved PIDs back to root)"
        ;;

    query)
        ver=$(detect_cg_version)
        if [ -f "$SIDECAR" ]; then
            { read -r base; read -r v; read -r orig; read -r extra; } < "$SIDECAR"
            if [ "$ver" = 2 ]; then
                cur=$(cat "$base/cpu.max" 2>/dev/null)
                cur_cpus=$(cat "$base/cpuset.cpus.effective" 2>/dev/null)
                procs=$(cat "$base/cgroup.procs" 2>/dev/null | tr '\n' ' ')
                echo "v2 $base: cpu.max='$cur' cpus='$cur_cpus'"
                echo "  procs: ${procs:-(none)}"
                case "$cur" in
                    max|"") exit 1;;
                    *) exit 0;;
                esac
            else
                cur=$(cat "$base/cpu.cfs_quota_us" 2>/dev/null)
                procs=$(cat "$base/tasks" 2>/dev/null | tr '\n' ' ')
                echo "v1 $base: cfs_quota_us='$cur'"
                echo "  tasks: ${procs:-(none)}"
                [ "$cur" != "-1" ] && [ -n "$cur" ]
            fi
        else
            echo "no active cpu_quota"
            exit 1
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
