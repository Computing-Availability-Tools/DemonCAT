#!/bin/sh
# rCPU_quota: limit CPU core max utilization (1-99%) via cgroup.
# inject: create cgroup, set cpuset.cpus=<cores> + cpu.max=<quota>, move target core's PIDs into it
# clean:  move PIDs back to root cgroup, restore, remove cgroup
# query:  show quota and actual core utilization

SIDECAR_PFX="/tmp/dcat-rCPU_quota"

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
        safe=$(echo "$cores" | tr -c 'a-zA-Z0-9' '_')
        SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
        PIDLIST="${SIDECAR_PFX}-${safe}.pids"
        ver=$(detect_cg_version)
        period=100000
        quota_us=$(( quota * period / 100 ))
        cpus=$(parse_cores "$cores" | tr '\n' ',' | sed 's/,$//')

        if [ "$ver" = 2 ]; then
            [ -f /sys/fs/cgroup/cgroup.controllers ] || { echo "cgroup v2 not mounted (/sys/fs/cgroup/cgroup.controllers missing)" >&2; exit 1; }
            base="/sys/fs/cgroup/dcat_quota_${safe}"
            mkdir -p "$base" 2>/dev/null || { echo "cannot mkdir $base (need root?)" >&2; exit 1; }
            echo "+cpu +cpuset" > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
            echo "$cpus" > "$base/cpuset.cpus" 2>/dev/null || true
            orig_max=$(cat "$base/cpu.max" 2>/dev/null)
            echo "$quota_us $period" > "$base/cpu.max" 2>/dev/null || { echo "set cpu.max failed" >&2; exit 1; }
            : > "$PIDLIST"
            moved=0
            for pid in $(find_pids_on_cores "$cores"); do
                if echo "$pid" > "$base/cgroup.procs" 2>/dev/null; then
                    echo "$pid" >> "$PIDLIST"; moved=$((moved + 1))
                fi
            done
            printf '%s\n2\n%s\n%s\n' "$base" "$orig_max" "$cpus" > "$SIDECAR"
            echo "set cpu.max=$quota_us $period, cpuset.cpus=$cpus on $base (moved $moved PIDs)"
            [ "$moved" -eq 0 ] && echo "WARNING: no PIDs found on cores [$cpus] — inject rCPU_overload first for the limit to take effect" >&2 || true
        else
            [ -d /sys/fs/cgroup/cpu ] || { echo "cgroup v1 cpu controller not found (/sys/fs/cgroup/cpu/)" >&2; exit 1; }
            [ -d /sys/fs/cgroup/cpuset ] || { echo "cgroup v1 cpuset controller not found (/sys/fs/cgroup/cpuset/)" >&2; exit 1; }
            base="/sys/fs/cgroup/cpu/dcat_quota_${safe}"
            cbase="/sys/fs/cgroup/cpuset/dcat_quota_${safe}"
            mkdir -p "$base" 2>/dev/null || { echo "cannot mkdir $base (need root?)" >&2; exit 1; }
            mkdir -p "$cbase" 2>/dev/null || true
            orig_q=$(cat "$base/cpu.cfs_quota_us" 2>/dev/null)
            echo "$quota_us" > "$base/cpu.cfs_quota_us" 2>/dev/null || { echo "set cfs_quota_us failed" >&2; exit 1; }
            echo "100000" > "$base/cpu.cfs_period_us" 2>/dev/null || true
            echo "$cpus" > "$cbase/cpuset.cpus" 2>/dev/null || true
            echo "0" > "$cbase/cpuset.mems" 2>/dev/null || true
            : > "$PIDLIST"
            moved=0
            for pid in $(find_pids_on_cores "$cores"); do
                echo "$pid" > "$cbase/tasks" 2>/dev/null || true
                if echo "$pid" > "$base/tasks" 2>/dev/null; then
                    echo "$pid" >> "$PIDLIST"; moved=$((moved + 1))
                fi
            done
            printf '%s\n1\n%s\n%s\n' "$base" "$orig_q" "$cbase" > "$SIDECAR"
            echo "set cfs_quota_us=$quota_us on $base, cpuset=$cpus on $cbase (moved $moved PIDs)"
            [ "$moved" -eq 0 ] && echo "WARNING: no PIDs found on cores [$cpus] — inject rCPU_overload first for the limit to take effect" >&2 || true
        fi
        ;;

    clean)
        clean_quota_file() {
            [ -f "$1" ] || return 1
            { read -r base; read -r ver; read -r orig; read -r extra; } < "$1"
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
            rm -f "$1" "${1%.sidecar}.pids"
        }
        if [ -n "${DCAT_PARAM_CORES:-}" ]; then
            safe=$(echo "$DCAT_PARAM_CORES" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            clean_quota_file "$SIDECAR" || { echo "no active cpu_quota" >&2; exit 1; }
            echo "cleaned cpu_quota (restored, moved PIDs back to root)"
        else
            found=0
            for f in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$f" ] || continue
                clean_quota_file "$f" && found=1
            done
            if [ "$found" -eq 0 ]; then
                echo "no active cpu_quota" >&2
                exit 1
            fi
            echo "cleaned all cpu_quota"
        fi
        ;;

    query)
        ver=$(detect_cg_version)
        query_one() {
            [ -f "$1" ] || return 1
            { read -r base; read -r v; read -r orig; read -r extra; } < "$1"
            if [ "$ver" = 2 ]; then
                cur=$(cat "$base/cpu.max" 2>/dev/null)
                cpus=$(cat "$base/cpuset.cpus.effective" 2>/dev/null)
            else
                cur=$(cat "$base/cpu.cfs_quota_us" 2>/dev/null)
                cpus=$(cat "$extra/cpuset.cpus" 2>/dev/null)
            fi
            quota_pct=$(( ${cur%% *} / 1000 ))
            [ "$quota_pct" -lt 1 ] 2>/dev/null && quota_pct=0
            echo "FAULT CONFIRMED: cores [$cpus] capped at ${quota_pct}% CPU"
            echo "--- actual core utilization (1s sample) ---"
            for n in $(parse_cores "$cpus" 2>/dev/null); do
                l1=$(grep "^cpu$n " /proc/stat)
                sleep 1
                l2=$(grep "^cpu$n " /proc/stat)
                t1=$(echo "$l1" | awk '{print $2+$3+$4+$5+$6+$7+$8+$9+$10}')
                i1=$(echo "$l1" | awk '{print $5}')
                t2=$(echo "$l2" | awk '{print $2+$3+$4+$5+$6+$7+$8+$9+$10}')
                i2=$(echo "$l2" | awk '{print $5}')
                dt=$((t2 - t1)); di=$((i2 - i1)); db=$((dt - di))
                [ "$dt" -gt 0 ] 2>/dev/null && pct=$((db * 100 / dt)) || pct=0
                echo "  core $n: ${pct}% (cap=${quota_pct}%)"
            done
            return 0
        }
        if [ -n "${DCAT_PARAM_CORES:-}" ]; then
            safe=$(echo "$DCAT_PARAM_CORES" | tr -c 'a-zA-Z0-9' '_')
            query_one "${SIDECAR_PFX}-${safe}.sidecar" && exit 0 || { echo "FAULT NOT ACTIVE: no cpu_quota" >&2; exit 1; }
        else
            found=0
            for f in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$f" ] || continue
                query_one "$f" && found=1
            done
            [ "$found" -eq 1 ] && exit 0 || { echo "no active cpu_quota"; exit 1; }
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
