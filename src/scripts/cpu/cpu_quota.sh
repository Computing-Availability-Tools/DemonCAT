#!/bin/sh
# rCPU_quota: cgroup CPU quota ceiling (1-99%).
# inject: set cpu.max (v2) or cpu.cfs_quota_us (v1) on a cgroup
# clean:  restore original (saved in sidecar), remove the cgroup we created
# query:  read the cgroup limit

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
        cg=${DCAT_PARAM_CG_PATH:-}
        ver=$(detect_cg_version)
        period=100000
        quota_us=$(( quota * period / 100 ))

        if [ "$ver" = 2 ]; then
            base=${cg:-/sys/fs/cgroup/dcat_quota}
            mkdir -p "$base" 2>/dev/null || { echo "cannot mkdir $base (need root?)" >&2; exit 1; }
            # enable cpu controller on parent if possible
            [ -f /sys/fs/cgroup/cgroup.subtree_control ] && echo +cpu > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
            orig=$(cat "$base/cpu.max" 2>/dev/null)
            echo "$quota_us $period" > "$base/cpu.max" 2>/dev/null || { echo "set cpu.max failed" >&2; exit 1; }
            printf '%s\n2\n%s\n%s\n' "$base" "$orig" "v2" > "$SIDECAR"
            echo "set v2 cpu.max=$quota_us $period on $base (was '$orig')"
        else
            base=${cg:-/sys/fs/cgroup/cpu/dcat_quota}
            mkdir -p "$base" 2>/dev/null || { echo "cannot mkdir $base (need root?)" >&2; exit 1; }
            orig=$(cat "$base/cpu.cfs_quota_us" 2>/dev/null)
            echo "$quota_us" > "$base/cpu.cfs_quota_us" 2>/dev/null || { echo "set cfs_quota_us failed" >&2; exit 1; }
            printf '%s\n1\n%s\nv1\n' "$base" "$orig" > "$SIDECAR"
            echo "set v1 cpu.cfs_quota_us=$quota_us on $base (was '$orig')"
        fi
        ;;

    clean)
        [ -f "$SIDECAR" ] || { echo "no active cpu_quota" >&2; exit 1; }
        { read -r base; read -r ver; read -r orig; } < "$SIDECAR"
        if [ "$ver" = 2 ]; then
            echo "${orig:-max}" > "$base/cpu.max" 2>/dev/null || true
            rmdir "$base" 2>/dev/null || true
        else
            echo "${orig:--1}" > "$base/cpu.cfs_quota_us" 2>/dev/null || true
            rmdir "$base" 2>/dev/null || true
        fi
        rm -f "$SIDECAR"
        echo "cleaned cpu_quota (restored $base)"
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
            case "$cur" in
                max|"") exit 1;;
                *) exit 0;;
            esac
        else
            cur=$(cat "$cg/cpu.cfs_quota_us" 2>/dev/null)
            echo "v1 $cg/cpu.cfs_quota_us='$cur'"
            [ "$cur" != "-1" ] && [ -n "$cur" ]
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
