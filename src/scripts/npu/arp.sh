#!/bin/sh
# rNPU_arp: ARP entry manipulation (add=poison, del=delete). Clean auto-undoes.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
action=${DCAT_PARAM_ACTION:-}
dev=${DCAT_PARAM_DEV:-}
ip=${DCAT_PARAM_IP:-}
mac=${DCAT_PARAM_MAC:-}
HCCN="hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_arp-$chip.bak"

is_del_action() { [ -f "$SIDECAR" ]; }

fault_present() {
    if is_del_action; then
        ! $HCCN -arp -g 2>/dev/null | grep -Fq "$ip"
    else
        $HCCN -arp -g 2>/dev/null | grep -F "$ip" | grep -Fq "$mac"
    fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${action:?missing required param: action (add or del)}
        : ${dev:?missing required param: dev}
        : ${ip:?missing required param: ip}
        npu_check_env
        case "$action" in
            add)
                : ${mac:?missing required param: mac (required for action=add)}
                $HCCN -arp -a dev "$dev" ip "$ip" mac "$mac" || { echo "arp add failed" >&2; exit 1; }
                fault_present || { echo "rNPU_arp 注入回读校验失败:动作未生效" >&2; exit 1; }
                echo "poisoned arp $dev/$ip -> $mac on chip $chip"
                ;;
            del)
                orig_mac=$($HCCN -arp -g 2>/dev/null | grep "$ip" | grep -oE 'at [0-9a-f:]+' | awk '{print $2}')
                printf '%s\n' "${orig_mac:-}" > "$SIDECAR"
                $HCCN -arp -d dev "$dev" ip "$ip" || { echo "arp del failed" >&2; exit 1; }
                fault_present || { echo "rNPU_arp 注入回读校验失败:动作未生效" >&2; exit 1; }
                echo "deleted arp $dev/$ip on chip $chip (was mac ${orig_mac:-none})"
                ;;
            *) echo "invalid action: $action (expected add or del)" >&2; exit 1 ;;
        esac
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for arp clean)"
        elif is_del_action; then
            orig_mac=$(cat "$SIDECAR" 2>/dev/null); orig_mac=${orig_mac:-00:00:00:00:00:00}
            $HCCN -arp -a dev "$dev" ip "$ip" mac "$orig_mac" || { echo "arp re-add failed" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "restored arp $dev/$ip -> $orig_mac on chip $chip"
        elif fault_present; then
            $HCCN -arp -d dev "$dev" ip "$ip" || { echo "arp del failed" >&2; exit 1; }
            echo "removed poisoned arp $dev/$ip on chip $chip"
        else echo "arp entry not present, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -arp -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
