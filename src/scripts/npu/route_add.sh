#!/bin/sh
# rNPU_route_add: add route. Clean = delete the added route.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
addr=${DCAT_PARAM_ADDRESS:-}
mask=${DCAT_PARAM_NETMASK:-}
gw=${DCAT_PARAM_GATEWAY:-}
HCCN="hccn_tool -i $chip"

fault_present() { [ -n "$addr" ] && $HCCN -route -g 2>/dev/null | grep -Fq "$addr"; }

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        $HCCN -route -a address "$addr" netmask "$mask" gateway "$gw" || { echo "route add failed" >&2; exit 1; }
        echo "added route $addr/$mask via $gw on chip $chip"
        ;;
    clean)
        if fault_present; then
            $HCCN -route -d address "$addr" netmask "$mask" || { echo "route del failed" >&2; exit 1; }
            echo "removed route $addr/$mask on chip $chip"
        else echo "route not present, no-op"; fi
        ;;
    query) $HCCN -route -g; fault_present ;;
esac
