#!/bin/sh
# rNPU_bw_limit: RoCE shaping bandwidth limit. Clean = restore original bw_limit from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
bw=${DCAT_PARAM_BW_LIMIT:-}
HCCN="hccn_tool -i $chip"

# Parse hccn_tool -shaping -g output: "bw_limit[200000 Mbps], bw_max[200000 Mbps], ..."
bw_cur() { $HCCN -shaping -g 2>/dev/null | grep -oE 'bw_limit\[[0-9]+' | grep -oE '[0-9]+'; }
bw_max() { $HCCN -shaping -g 2>/dev/null | grep -oE 'bw_max\[[0-9]+' | grep -oE '[0-9]+'; }

fault_present() {
    cur=$(bw_cur)
    orig=$(sidecar_load rNPU_bw_limit "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${bw:?missing required param: bw_limit}
        npu_check_env
        orig=$(bw_cur)
        [ -n "$orig" ] && sidecar_save rNPU_bw_limit "$chip" "$orig"
        $HCCN -shaping -s bw_limit "$bw" || { echo "shaping set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_bw_limit 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied bw_limit $bw on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_bw_limit-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_bw_limit-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored bw_limit (all chips)" || echo "restored bw_limit (no active injection)"
        elif fault_present; then
            $HCCN -shaping -s bw_limit "$orig" || { echo "shaping restore failed" >&2; exit 1; }
            sidecar_clear rNPU_bw_limit "$chip"
            echo "restored bw_limit to $orig on chip $chip"
        else echo "bw_limit already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -shaping -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
