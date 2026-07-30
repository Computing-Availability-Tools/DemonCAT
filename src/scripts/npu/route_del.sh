#!/bin/sh
# rNPU_route_del: delete route. Clean = re-add with original gateway from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
addr=${DCAT_PARAM_ADDRESS:-}
mask=${DCAT_PARAM_NETMASK:-}
HCCN="hccn_tool -i $chip"

fault_present() { ! $HCCN -route -g 2>/dev/null | grep -Fq "$addr"; }

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        orig_gw=$($HCCN -route -g 2>/dev/null | awk -v a="$addr" '$1==a {print $2}')
        [ -n "$orig_gw" ] && [ "$orig_gw" != "*" ] && sidecar_save rNPU_route_del "$chip" "$orig_gw"
        $HCCN -route -d address "$addr" netmask "$mask" || { echo "route del failed" >&2; exit 1; }
        echo "deleted route $addr/$mask on chip $chip (was via $orig_gw)"
        ;;
    clean)
        if fault_present; then
            orig_gw=$(sidecar_load rNPU_route_del "$chip"); orig_gw=${orig_gw:-0.0.0.0}
            $HCCN -route -a address "$addr" netmask "$mask" gateway "$orig_gw" || { echo "route re-add failed" >&2; exit 1; }
            sidecar_clear rNPU_route_del "$chip"
            echo "restored route $addr/$mask via $orig_gw on chip $chip"
        else echo "route already present, no-op"; fi
        ;;
    query) $HCCN -route -g; fault_present ;;
esac
