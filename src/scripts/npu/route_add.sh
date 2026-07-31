#!/bin/sh
# rNPU_route_add: add route. Clean = delete the added route.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
addr=${DCAT_PARAM_ADDRESS:-}
mask=${DCAT_PARAM_NETMASK:-}
gw=${DCAT_PARAM_GATEWAY:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    if [ -n "$addr" ]; then $HCCN -route -g 2>/dev/null | grep -Fq "$addr"
    else [ -f "/tmp/dcat-rNPU_route_add-$chip.bak" ]; fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${addr:?missing required param: address}
        : ${mask:?missing required param: netmask}
        : ${gw:?missing required param: gateway}
        npu_check_env
        $HCCN -route -a address "$addr" netmask "$mask" gateway "$gw" || { echo "route add failed" >&2; exit 1; }
        sidecar_save rNPU_route_add "$chip" "$addr"
        echo "added route $addr/$mask via $gw on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for route clean)"
        elif fault_present; then
            $HCCN -route -d address "$addr" netmask "$mask" || { echo "route del failed" >&2; exit 1; }
            sidecar_clear rNPU_route_add "$chip"
            echo "removed route $addr/$mask on chip $chip"
        else echo "route not present, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -route -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
