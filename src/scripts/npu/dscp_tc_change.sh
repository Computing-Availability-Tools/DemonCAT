#!/bin/sh
# rNPU_dscp_tc_change: DSCP-to-TC mapping change. Clean = restore original tc.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
dscp=${DCAT_PARAM_DSCP:-}
tc=${DCAT_PARAM_TC:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    [ -n "${dscp:-}" ] || { [ -f "/tmp/dcat-rNPU_dscp_tc_change-$chip.bak" ]; return $?; }
    cur=$($HCCN -dscp_to_tc -g dscp "$dscp" 2>/dev/null | awk -v d="$dscp" '$1==d {print $2}')
    orig=$(sidecar_load rNPU_dscp_tc_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${dscp:?missing required param: dscp}
        : ${tc:?missing required param: tc}
        npu_check_env
        orig=$($HCCN -dscp_to_tc -g dscp "$dscp" 2>/dev/null | awk -v d="$dscp" '$1==d {print $2}')
        [ -n "$orig" ] && sidecar_save rNPU_dscp_tc_change "$chip" "$orig"
        $HCCN -dscp_to_tc -s dscp "$dscp" tc "$tc" || { echo "dscp_to_tc set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_dscp_tc_change 注入回读校验失败:动作未生�? >&2; exit 1; }
        echo "applied dscp $dscp -> tc $tc on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for dscp_to_tc clean)"
        elif fault_present; then
            orig=$(sidecar_load rNPU_dscp_tc_change "$chip"); orig=${orig:-0}
            $HCCN -dscp_to_tc -s dscp "$dscp" tc "$orig" || { echo "dscp_to_tc restore failed" >&2; exit 1; }
            sidecar_clear rNPU_dscp_tc_change "$chip"
            echo "restored dscp $dscp -> tc $orig on chip $chip"
        else echo "dscp_to_tc already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -dscp_to_tc -g dscp "$dscp"; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
