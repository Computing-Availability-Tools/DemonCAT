#!/bin/sh
# rNPU_bw_limit: RoCE shaping bandwidth limit. Clean = restore original bw_limit from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
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
        npu_check_env
        orig=$(bw_cur)
        [ -n "$orig" ] && sidecar_save rNPU_bw_limit "$chip" "$orig"
        $HCCN -shaping -s bw_limit "$bw" || { echo "shaping set failed" >&2; exit 1; }
        echo "applied bw_limit $bw on chip $chip (was $orig)"
        ;;
    clean)
        if fault_present; then
            orig=$(sidecar_load rNPU_bw_limit "$chip"); orig=${orig:-$(bw_max)}
            $HCCN -shaping -s bw_limit "$orig" || { echo "shaping restore failed" >&2; exit 1; }
            sidecar_clear rNPU_bw_limit "$chip"
            echo "restored bw_limit to $orig on chip $chip"
        else echo "bw_limit already at original, no-op"; fi
        ;;
    query) $HCCN -shaping -g; fault_present ;;
esac
