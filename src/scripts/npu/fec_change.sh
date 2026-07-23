#!/bin/sh
# rNPU_fec_change: RoCE FEC mode change. Clean = restore original FEC from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
enc=${DCAT_PARAM_ENCODING:?missing required param: encoding}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -fec -g 2>/dev/null | grep -oiE 'encoding [a-z]+' | grep -oiE '[a-z]+$')
    orig=$(sidecar_load rNPU_fec_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        orig=$($HCCN -fec -g 2>/dev/null | grep -oiE 'encoding [a-z]+' | grep -oiE '[a-z]+$')
        [ -n "$orig" ] && sidecar_save rNPU_fec_change "$chip" "$orig"
        $HCCN -fec -s encoding "$enc" || { echo "fec set failed" >&2; exit 1; }
        echo "applied fec $enc on chip $chip (was $orig)"
        ;;
    clean)
        if fault_present; then
            orig=$(sidecar_load rNPU_fec_change "$chip"); orig=${orig:-rs}
            $HCCN -fec -s encoding "$orig" || { echo "fec restore failed" >&2; exit 1; }
            sidecar_clear rNPU_fec_change "$chip"
            echo "restored fec to $orig on chip $chip"
        else echo "fec already at original, no-op"; fi
        ;;
    query) $HCCN -fec -g; fault_present ;;
esac
