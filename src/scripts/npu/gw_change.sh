#!/bin/sh
# rNPU_gw_change: RoCE gateway change. Clean = restore original gateway from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
npu_validate_chip "$chip"
gw=${DCAT_PARAM_GATEWAY:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -gateway -g 2>/dev/null | grep -oE 'gateway:[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+')
    orig=$(sidecar_load rNPU_gw_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        orig=$($HCCN -gateway -g 2>/dev/null | grep -oE 'gateway:[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+')
        [ -n "$orig" ] && sidecar_save rNPU_gw_change "$chip" "$orig"
        $HCCN -gateway -s gateway "$gw" || { echo "gateway set failed" >&2; exit 1; }
        echo "applied gateway $gw on chip $chip (was $orig)"
        ;;
    clean)
        if fault_present; then
            orig=$(sidecar_load rNPU_gw_change "$chip"); orig=${orig:-0.0.0.0}
            $HCCN -gateway -s gateway "$orig" || { echo "gateway restore failed" >&2; exit 1; }
            sidecar_clear rNPU_gw_change "$chip"
            echo "restored gateway to $orig on chip $chip"
        else echo "gateway already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -gateway -g; fault_present' ;;
esac
