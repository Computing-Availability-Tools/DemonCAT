#!/bin/sh
# rNPU_route: RoCE route manipulation (add/delete). Clean auto-undoes.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
action=${DCAT_PARAM_ACTION:-}
addr=${DCAT_PARAM_ADDRESS:-}
mask=${DCAT_PARAM_NETMASK:-}
gw=${DCAT_PARAM_GATEWAY:-}
HCCN="hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_route-$chip.bak"

is_del_action() { [ -f "$SIDECAR" ]; }

fault_present() {
    if is_del_action; then
        ! $HCCN -route -g 2>/dev/null | grep -Fq "$addr"
    else
        $HCCN -route -g 2>/dev/null | grep -Fq "$addr"
    fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${action:?missing required param: action (add or del)}
        : ${addr:?missing required param: address}
        : ${mask:?missing required param: netmask}
        npu_check_env
        case "$action" in
            add)
                : ${gw:?missing required param: gateway (required for action=add)}
                $HCCN -route -a address "$addr" netmask "$mask" gateway "$gw" || { echo "route add failed" >&2; exit 1; }
                fault_present || { echo "rNPU_route 注入回读校验失败:动作未生效" >&2; exit 1; }
                echo "added route $addr/$mask via $gw on chip $chip"
                ;;
            del)
                orig_gw=$($HCCN -route -g 2>/dev/null | awk -v a="$addr" '$1==a {print $2}')
                [ "$orig_gw" = "*" ] && orig_gw=""
                printf '%s\n' "${orig_gw:-}" > "$SIDECAR"
                $HCCN -route -d address "$addr" netmask "$mask" || { echo "route del failed" >&2; exit 1; }
                fault_present || { echo "rNPU_route 注入回读校验失败:动作未生效" >&2; exit 1; }
                echo "deleted route $addr/$mask on chip $chip (was via ${orig_gw:-none})"
                ;;
            *) echo "invalid action: $action (expected add or del)" >&2; exit 1 ;;
        esac
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for route clean)"
        elif is_del_action; then
            orig_gw=$(cat "$SIDECAR" 2>/dev/null); orig_gw=${orig_gw:-0.0.0.0}
            $HCCN -route -a address "$addr" netmask "$mask" gateway "$orig_gw" || { echo "route re-add failed" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "restored route $addr/$mask via $orig_gw on chip $chip"
        elif fault_present; then
            $HCCN -route -d address "$addr" netmask "$mask" || { echo "route del failed" >&2; exit 1; }
            echo "removed route $addr/$mask on chip $chip"
        else echo "route not present, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -route -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
