#!/bin/sh
# rNPU_prio_tc_change: prio-to-TC mapping change. Clean = restore original 8-csv map.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
map=${DCAT_PARAM_MAP:?missing required param: map}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -prio_tc -g 2>/dev/null | grep -oE 'map [0-9,]+' | grep -oE '[0-9,]+')
    orig=$(sidecar_load rNPU_prio_tc_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        orig=$($HCCN -prio_tc -g 2>/dev/null | grep -oE 'map [0-9,]+' | grep -oE '[0-9,]+')
        [ -n "$orig" ] && sidecar_save rNPU_prio_tc_change "$chip" "$orig"
        $HCCN -prio_tc -s map "$map" || { echo "prio_tc set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_prio_tc_change 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied prio_tc map $map on chip $chip (was $orig)"
        ;;
    clean)
        if fault_present; then
            orig=$(sidecar_load rNPU_prio_tc_change "$chip"); orig=${orig:-0,0,0,0,0,0,0,0}
            $HCCN -prio_tc -s map "$orig" || { echo "prio_tc restore failed" >&2; exit 1; }
            sidecar_clear rNPU_prio_tc_change "$chip"
            echo "restored prio_tc map to $orig on chip $chip"
        else echo "prio_tc already at original, no-op"; fi
        ;;
    query) $HCCN -prio_tc -g; fault_present ;;
esac
