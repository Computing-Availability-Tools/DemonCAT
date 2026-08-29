#!/bin/sh
# rNPU_dscp_tc_change: DSCP-to-TC mapping change. Clean = restore original tc.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
dscp=${DCAT_PARAM_DSCP:-}
tc=${DCAT_PARAM_TC:-}
HCCN="hccn"

fault_present() {
    [ -n "${dscp:-}" ] || { [ -f "/tmp/dcat-rNPU_dscp_tc_change-$chip.bak" ]; return $?; }
    cur=$($HCCN -dscp_to_tc -g dscp "$dscp" 2>/dev/null | awk -v d="$dscp" '$1==d {print $2}')
    orig=$(grep '^orig=' "/tmp/dcat-rNPU_dscp_tc_change-$chip.bak" 2>/dev/null | cut -d= -f2-)
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${dscp:?missing required param: dscp}
        : ${tc:?missing required param: tc}
        npu_check_env
        orig=$($HCCN -dscp_to_tc -g dscp "$dscp" 2>/dev/null | awk -v d="$dscp" '$1==d {print $2}')
        [ -n "$orig" ] && printf 'dscp=%s\norig=%s\n' "$dscp" "$orig" > "/tmp/dcat-rNPU_dscp_tc_change-$chip.bak"
        $HCCN -dscp_to_tc -s dscp "$dscp" tc "$tc" || { echo "dscp_to_tc set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_dscp_tc_change 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied dscp $dscp -> tc $tc on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            failed=0
            for bak in /tmp/dcat-rNPU_dscp_tc_change-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_dscp_tc_change-}; c=${c%.bak}
                if DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1; then
                    cleaned=1
                else
                    echo "restore failed for chip $c" >&2
                    failed=1
                fi
            done
            if [ "$failed" = 1 ]; then
                echo "dscp_to_tc: some restores failed (state preserved)" >&2; exit 1
            elif [ "$cleaned" = 1 ]; then
                echo "restored dscp_to_tc (all chips)"
            else
                echo "restored dscp_to_tc (no active injection)"
            fi
        elif fault_present; then
            dscp=$(grep '^dscp=' "/tmp/dcat-rNPU_dscp_tc_change-$chip.bak" 2>/dev/null | cut -d= -f2-)
            orig=$(grep '^orig=' "/tmp/dcat-rNPU_dscp_tc_change-$chip.bak" 2>/dev/null | cut -d= -f2-); orig=${orig:-0}
            $HCCN -dscp_to_tc -s dscp "$dscp" tc "$orig" || { echo "dscp_to_tc restore failed" >&2; exit 1; }
            rm -f "/tmp/dcat-rNPU_dscp_tc_change-$chip.bak"
            echo "restored dscp $dscp -> tc $orig on chip $chip"
        else echo "dscp_to_tc already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -dscp_to_tc -g dscp "$dscp"; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
