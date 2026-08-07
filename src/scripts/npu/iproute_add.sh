#!/bin/sh
# rNPU_iproute_add: add ip route. Clean = delete the added route.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
ip=${DCAT_PARAM_IP:-}
mask=${DCAT_PARAM_IP_MASK:-}
via=${DCAT_PARAM_VIA:-}
dev=${DCAT_PARAM_DEV:-}
table=${DCAT_PARAM_TABLE:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    if [ -n "$ip" ] && [ -n "$table" ]; then $HCCN -ip_route -g table "$table" 2>/dev/null | grep -Fq "$ip"
    else [ -f "/tmp/dcat-rNPU_iproute_add-$chip.bak" ]; fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${ip:?missing required param: ip}
        : ${mask:?missing required param: ip_mask}
        : ${via:?missing required param: via}
        : ${dev:?missing required param: dev}
        : ${table:?missing required param: table}
        npu_check_env
        $HCCN -ip_route -a ip "$ip" ip_mask "$mask" via "$via" dev "$dev" table "$table" || { echo "ip_route add failed" >&2; exit 1; }
        fault_present || { echo "rNPU_iproute_add 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "added ip_route $ip/$mask via $via dev $dev table $table on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for ip_route clean)"
        elif fault_present; then
            $HCCN -ip_route -d ip "$ip" ip_mask "$mask" table "$table" || { echo "ip_route del failed" >&2; exit 1; }
            sidecar_clear rNPU_iproute_add "$chip"
            echo "removed ip_route $ip/$mask table $table on chip $chip"
        else echo "ip_route not present, no-op"; fi
        ;;
    query) npu_foreach_chip '[ -n "$table" ] && $HCCN -ip_route -g table "$table"; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
