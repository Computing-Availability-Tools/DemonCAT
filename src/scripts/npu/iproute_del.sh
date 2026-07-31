#!/bin/sh
# rNPU_iproute_del: delete ip route. Clean = re-add with original via/dev from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
npu_validate_chip "$chip"
ip=${DCAT_PARAM_IP:-}
mask=${DCAT_PARAM_IP_MASK:-}
table=${DCAT_PARAM_TABLE:-}
HCCN="hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_iproute_del-$chip.bak"

fault_present() { ! $HCCN -ip_route -g table "$table" 2>/dev/null | grep -Fq "$ip"; }

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        cur=$($HCCN -ip_route -g table "$table" 2>/dev/null | grep "$ip")
        o_via=$(echo "$cur" | grep -oE 'via [0-9.]+' | awk '{print $2}')
        o_dev=$(echo "$cur" | grep -oE 'dev [a-z0-9]+' | awk '{print $2}')
        [ -n "$o_via" ] && printf 'via=%s\ndev=%s\n' "$o_via" "$o_dev" > "$SIDECAR"
        $HCCN -ip_route -d ip "$ip" ip_mask "$mask" table "$table" || { echo "ip_route del failed" >&2; exit 1; }
        echo "deleted ip_route $ip/$mask table $table on chip $chip (was via $o_via dev $o_dev)"
        ;;
    clean)
        if fault_present; then
            . "$SIDECAR" 2>/dev/null
            : ${via:=0.0.0.0}; : ${dev:=eth0}
            $HCCN -ip_route -a ip "$ip" ip_mask "$mask" via "$via" dev "$dev" table "$table" || { echo "ip_route re-add failed" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "restored ip_route $ip/$mask via $via dev $dev table $table on chip $chip"
        else echo "ip_route already present, no-op"; fi
        ;;
    query) $HCCN -ip_route -g table "$table"; fault_present ;;
esac
