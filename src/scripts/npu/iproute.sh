#!/bin/sh
# rNPU_iproute: ip route manipulation (add/delete). Clean auto-undoes.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
action=${DCAT_PARAM_ACTION:-}
ip=${DCAT_PARAM_IP:-}
mask=${DCAT_PARAM_IP_MASK:-}
via=${DCAT_PARAM_VIA:-}
dev=${DCAT_PARAM_DEV:-}
table=${DCAT_PARAM_TABLE:-}
HCCN="hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_iproute-$chip.bak"

is_del_action() { [ -f "$SIDECAR" ]; }

fault_present() {
    if is_del_action; then
        ! $HCCN -ip_route -g table "$table" 2>/dev/null | grep -Fq "$ip"
    else
        $HCCN -ip_route -g table "$table" 2>/dev/null | grep -Fq "$ip"
    fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${action:?missing required param: action (add or del)}
        : ${ip:?missing required param: ip}
        : ${mask:?missing required param: ip_mask}
        : ${table:?missing required param: table}
        npu_check_env
        case "$action" in
            add)
                : ${via:?missing required param: via (required for action=add)}
                : ${dev:?missing required param: dev (required for action=add)}
                $HCCN -ip_route -a ip "$ip" ip_mask "$mask" via "$via" dev "$dev" table "$table" || { echo "ip_route add failed" >&2; exit 1; }
                fault_present || { echo "rNPU_iproute 注入回读校验失败:动作未生效" >&2; exit 1; }
                echo "added ip_route $ip/$mask via $via dev $dev table $table on chip $chip"
                ;;
            del)
                cur=$($HCCN -ip_route -g table "$table" 2>/dev/null | grep "$ip")
                o_via=$(echo "$cur" | grep -oE 'via [0-9.]+' | awk '{print $2}')
                o_dev=$(echo "$cur" | grep -oE 'dev [a-z0-9]+' | awk '{print $2}')
                printf 'via=%s\ndev=%s\n' "${o_via:-}" "${o_dev:-}" > "$SIDECAR"
                $HCCN -ip_route -d ip "$ip" ip_mask "$mask" table "$table" || { echo "ip_route del failed" >&2; exit 1; }
                fault_present || { echo "rNPU_iproute 注入回读校验失败:动作未生效" >&2; exit 1; }
                echo "deleted ip_route $ip/$mask table $table on chip $chip (was via ${o_via:-none} dev ${o_dev:-none})"
                ;;
            *) echo "invalid action: $action (expected add or del)" >&2; exit 1 ;;
        esac
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_iproute-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_iproute-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored ip_route (all chips)" || echo "restored ip_route (no active injection)"
        elif is_del_action; then
            via=$(grep '^via=' "$SIDECAR" 2>/dev/null | cut -d= -f2)
            dev=$(grep '^dev=' "$SIDECAR" 2>/dev/null | cut -d= -f2)
            : ${via:=0.0.0.0}; : ${dev:=eth0}
            $HCCN -ip_route -a ip "$ip" ip_mask "$mask" via "$via" dev "$dev" table "$table" || { echo "ip_route re-add failed" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "restored ip_route $ip/$mask via $via dev $dev table $table on chip $chip"
        elif fault_present; then
            $HCCN -ip_route -d ip "$ip" ip_mask "$mask" table "$table" || { echo "ip_route del failed" >&2; exit 1; }
            echo "removed ip_route $ip/$mask table $table on chip $chip"
        else echo "ip_route not present, no-op"; fi
        ;;
    query) npu_foreach_chip '[ -n "$table" ] && $HCCN -ip_route -g table "$table"; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
