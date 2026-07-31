#!/bin/sh
# rNPU_prio_tc_change: prio-to-TC mapping change. Clean = restore original 8-csv map.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
map=${DCAT_PARAM_MAP:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -prio_tc -g 2>/dev/null | grep -oE 'map [0-9,]+' | grep -oE '[0-9,]+')
    orig=$(sidecar_load rNPU_prio_tc_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${map:?missing required param: map}
        npu_check_env
        $HCCN -link -g 2>/dev/null | grep -qi 'up' || { echo "link is DOWN, prio_tc requires link UP" >&2; exit 1; }
        orig=$($HCCN -prio_tc -g 2>/dev/null | grep -oE 'map [0-9,]+' | grep -oE '[0-9,]+')
        [ -n "$orig" ] && sidecar_save rNPU_prio_tc_change "$chip" "$orig"
        $HCCN -prio_tc -s map "$map" || { echo "prio_tc set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_prio_tc_change 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied prio_tc map $map on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_prio_tc_change-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_prio_tc_change-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored prio_tc map (all chips)" || echo "restored prio_tc map (no active injection)"
        elif fault_present; then
            orig=$(sidecar_load rNPU_prio_tc_change "$chip"); orig=${orig:-0,0,0,0,0,0,0,0}
            $HCCN -prio_tc -s map "$orig" || { echo "prio_tc restore failed" >&2; exit 1; }
            sidecar_clear rNPU_prio_tc_change "$chip"
            echo "restored prio_tc map to $orig on chip $chip"
        else echo "prio_tc already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -prio_tc -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
