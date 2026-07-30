#!/bin/sh
# rNPU_iproute_add: add ip route. Clean = delete the added route.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
ip=${DCAT_PARAM_IP:?missing required param: ip}
mask=${DCAT_PARAM_IP_MASK:?missing required param: ip_mask}
via=${DCAT_PARAM_VIA:?missing required param: via}
dev=${DCAT_PARAM_DEV:?missing required param: dev}
table=${DCAT_PARAM_TABLE:?missing required param: table}
HCCN="hccn_tool -i $chip"

fault_present() { $HCCN -ip_route -g table "$table" 2>/dev/null | grep -Fq "$ip"; }

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        $HCCN -ip_route -a ip "$ip" ip_mask "$mask" via "$via" dev "$dev" table "$table" || { echo "ip_route add failed" >&2; exit 1; }
        fault_present || { echo "rNPU_iproute_add 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "added ip_route $ip/$mask via $via dev $dev table $table on chip $chip"
        ;;
    clean)
        if fault_present; then
            $HCCN -ip_route -d ip "$ip" ip_mask "$mask" table "$table" || { echo "ip_route del failed" >&2; exit 1; }
            echo "removed ip_route $ip/$mask table $table on chip $chip"
        else echo "ip_route not present, no-op"; fi
        ;;
    query) $HCCN -ip_route -g table "$table"; fault_present ;;
esac
