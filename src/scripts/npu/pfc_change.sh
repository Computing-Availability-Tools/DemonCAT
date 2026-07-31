#!/bin/sh
# rNPU_pfc_change: PFC bitmap change. Clean = restore original 8-csv bitmap.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
bitmap=${DCAT_PARAM_BITMAP:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -pfc -g 2>/dev/null | grep -oE 'bitmap [0-9,]+' | grep -oE '[0-9,]+')
    orig=$(sidecar_load rNPU_pfc_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${bitmap:?missing required param: bitmap}
        npu_check_env
        orig=$($HCCN -pfc -g 2>/dev/null | grep -oE 'bitmap [0-9,]+' | grep -oE '[0-9,]+')
        [ -n "$orig" ] && sidecar_save rNPU_pfc_change "$chip" "$orig"
        $HCCN -pfc -s bitmap "$bitmap" || { echo "pfc set failed" >&2; exit 1; }
        echo "applied pfc bitmap $bitmap on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_pfc_change-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_pfc_change-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored pfc bitmap (all chips)" || echo "restored pfc bitmap (no active injection)"
        elif fault_present; then
            orig=$(sidecar_load rNPU_pfc_change "$chip"); orig=${orig:-0,0,0,0,0,0,0,0}
            $HCCN -pfc -s bitmap "$orig" || { echo "pfc restore failed" >&2; exit 1; }
            sidecar_clear rNPU_pfc_change "$chip"
            echo "restored pfc bitmap to $orig on chip $chip"
        else echo "pfc already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -pfc -g; fault_present' ;;
esac
