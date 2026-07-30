#!/bin/sh
# rNPU_bw_limit: RoCE shaping bandwidth limit. Clean = set bw_limit to max (100000).
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
bw=${DCAT_PARAM_BW_LIMIT:-}
HCCN="hccn_tool -i $chip"
MAX_BW=100000

fault_present() {
    cur=$($HCCN -shaping -g 2>/dev/null | grep -oE 'bw_limit [0-9]+' | grep -oE '[0-9]+')
    [ -n "$cur" ] && [ "$cur" -lt "$MAX_BW" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${bw:?missing required param: bw_limit}
        npu_check_env
        $HCCN -shaping -s bw_limit "$bw" || { echo "shaping set failed" >&2; exit 1; }
        echo "applied bw_limit $bw on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for bw_limit clean)"
        elif fault_present; then
            $HCCN -shaping -s bw_limit "$MAX_BW" || { echo "shaping restore failed" >&2; exit 1; }
            sidecar_clear rNPU_bw_limit "$chip"
            echo "restored bw_limit to $MAX_BW on chip $chip"
        else echo "already at max bandwidth, no-op"; fi
        ;;
    query) $HCCN -shaping -g; fault_present ;;
esac
