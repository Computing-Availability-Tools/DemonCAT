#!/bin/sh
# rCPU_quota: cgroup CPU quota ceiling (1-99%).
# Limits a target PROCESS (by PID) to quota_pct% CPU via cgroup.
# inject: create cgroup, set cpu.max, move PID(s) into it
# clean:  move PID(s) back to root cgroup, restore cpu.max, remove cgroup
# query:  show current quota and PIDs in the cgroup

SIDECAR="/tmp/dcat-rCPU_quota.sidecar"

detect_cg_version() {
    [ -f /sys/fs/cgroup/cgroup.controllers ] && echo 2 || echo 1
}

case "${DCAT_OP:-inject}" in
    inject)
        quota=${DCAT_PARAM_QUOTA_PCT:?missing required param: quota_pct}
        case "$quota" in
            *[!0-9]*|"") echo "quota_pct must be an integer 1-99, got: '$quota'" >&2; exit 1;;
        esac
        if [ "$quota" -lt 1 ] || [ "$quota" -gt 99 ]; then
            echo "quota_pct must be 1-99, got: $quota" >&2; exit 1
        fi
        pids=${DCAT_PARAM_PID:?missing required param: pid (comma-separated, e.g. --pid=12345 or --pid=123,456)}
        cg=${DCAT_PARAM_CG_PATH:-}
        ver=$(detect_cg_version)
        period=100000
        quota_us=$(( quota * period / 100 ))

        if [ "$ver" = 2 ]; then
            base=${cg:-/sys/fs/cgroup/dcat_quota}
            mkdir -p "$base" 2>/dev/null || { echo "cannot mkdir $base (need root?)" >&2; exit 1; }
            [ -f /sys/fs/cgroup/cgroup.subtree_control ] && echo +cpu > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
            orig=$(cat "$base/cpu.max" 2>/dev/null)
            echo "$quota_us $period" > "$base/cpu.max" 2>/dev/null || { echo "set cpu.max failed" >&2; exit 1; }
            echo "--- moving PIDs into $base ---"
            for p in $(echo "$pids" | tr ',' ' '); do
                echo "$p" > "$base/cgroup.procs" 2>/dev/null || echo "  warn: cannot move pid $p" >&2
            done
            printf '%s\n2\n%s\n%s\n' "$base" "$orig" > "$SIDECAR"
            echo "set v2 cpu.max=$quota_us $period on $base (was '$orig'), pids: $pids"
        else
            base=${cg:-/sys/fs/cgroup/cpu/dcat_quota}
            mkdir -p "$base" 2>/dev/null || { echo "cannot mkdir $base (need root?)" >&2; exit 1; }
            orig=$(cat "$base/cpu.cfs_quota_us" 2>/dev/null)
            echo "$quota_us" > "$base/cpu.cfs_quota_us" 2>/dev/null || { echo "set cfs_quota_us failed" >&2; exit 1; }
            for p in $(echo "$pids" | tr ',' ' '); do
                echo "$p" > "$base/tasks" 2>/dev/null || echo "  warn: cannot move pid $p" >&2
            done
            printf '%s\n1\n%s\n' "$base" "$orig" > "$SIDECAR"
            echo "set v1 cpu.cfs_quota_us=$quota_us on $base (was '$orig'), pids: $pids"
        fi
        ;;

    clean)
        [ -f "$SIDECAR" ] || { echo "no active cpu_quota" >&2; exit 1; }
        { read -r base; read -r ver; read -r orig; } < "$SIDECAR"
        if [ "$ver" = 2 ]; then
            # Move all procs back to root cgroup
            if [ -f "$base/cgroup.procs" ]; then
                while read -r p; do
                    [ -n "$p" ] && echo "$p" > /sys/fs/cgroup/cgroup.procs 2>/dev/null
                done < "$base/cgroup.procs"
            fi
            echo "${orig:-max}" > "$base/cpu.max" 2>/dev/null || true
            rmdir "$base" 2>/dev/null || true
        else
            if [ -f "$base/tasks" ]; then
                while read -r p; do
                    [ -n "$p" ] && echo "$p" > /sys/fs/cgroup/cpu/tasks 2>/dev/null
                done < "$base/tasks"
            fi
            echo "${orig:--1}" > "$base/cpu.cfs_quota_us" 2>/dev/null || true
            rmdir "$base" 2>/dev/null || true
        fi
        rm -f "$SIDECAR"
        echo "cleaned cpu_quota (restored $base, moved pids back to root)"
        ;;

    query)
        ver=$(detect_cg_version)
        cg=${DCAT_PARAM_CG_PATH:-}
        if [ -z "$cg" ] && [ -f "$SIDECAR" ]; then
            { read -r cg; } < "$SIDECAR"
        fi
        [ -n "$cg" ] || cg=/sys/fs/cgroup/dcat_quota
        if [ "$ver" = 2 ]; then
            cur=$(cat "$cg/cpu.max" 2>/dev/null)
            echo "v2 $cg/cpu.max='$cur'"
            if [ -f "$cg/cgroup.procs" ]; then
                procs=$(cat "$cg/cgroup.procs" 2>/dev/null | tr '\n' ' ')
                echo "  procs: ${procs:-(none)}"
            fi
            case "$cur" in
                max|"") exit 1;;
                *) exit 0;;
            esac
        else
            cur=$(cat "$cg/cpu.cfs_quota_us" 2>/dev/null)
            echo "v1 $cg/cpu.cfs_quota_us='$cur'"
            if [ -f "$cg/tasks" ]; then
                procs=$(cat "$cg/tasks" 2>/dev/null | tr '\n' ' ')
                echo "  tasks: ${procs:-(none)}"
            fi
            [ "$cur" != "-1" ] && [ -n "$cur" ]
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
