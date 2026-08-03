#!/bin/sh
# rNPU_gw_change: RoCE gateway change. Clean = restore original gateway from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
gw=${DCAT_PARAM_GATEWAY:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -gateway -g 2>/dev/null | grep -oE 'gateway:[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+')
    orig=$(sidecar_load rNPU_gw_change "$chip")
    [ -n "$cur" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${gw:?missing required param: gateway}
        npu_check_env
        orig=$($HCCN -gateway -g 2>/dev/null | grep -oE 'gateway:[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+')
        sidecar_save rNPU_gw_change "$chip" "${orig:-none}"
        $HCCN -gateway -s gateway "$gw" || { echo "gateway set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_gw_change 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied gateway $gw on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_gw_change-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_gw_change-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored gateway (all chips)" || echo "restored gateway (no active injection)"
        elif fault_present; then
            orig=$(sidecar_load rNPU_gw_change "$chip")
            if [ "$orig" = "none" ] || [ -z "$orig" ]; then
                sidecar_clear rNPU_gw_change "$chip"
                echo "no original gateway to restore on chip $chip"
            else
                $HCCN -gateway -s gateway "$orig" || { echo "gateway restore failed" >&2; exit 1; }
                sidecar_clear rNPU_gw_change "$chip"
                echo "restored gateway to $orig on chip $chip"
            fi
        else echo "gateway already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -gateway -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
