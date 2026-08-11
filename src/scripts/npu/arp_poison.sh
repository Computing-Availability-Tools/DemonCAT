#!/bin/sh
# rNPU_arp_poison: add wrong ARP entry. Clean = delete the added entry.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
dev=${DCAT_PARAM_DEV:-}
ip=${DCAT_PARAM_IP:-}
mac=${DCAT_PARAM_MAC:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    if [ -n "$ip" ] && [ -n "$mac" ]; then $HCCN -arp -g 2>/dev/null | grep -F "$ip" | grep -Fq "$mac"
    else [ -f "/tmp/dcat-rNPU_arp_poison-$chip.bak" ]; fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${dev:?missing required param: dev}
        : ${ip:?missing required param: ip}
        : ${mac:?missing required param: mac}
        npu_check_env
        $HCCN -arp -a dev "$dev" ip "$ip" mac "$mac" || { echo "arp add failed" >&2; exit 1; }
        fault_present || { echo "rNPU_arp_poison 注入回读校验失败:动作未生�? >&2; exit 1; }
        echo "poisoned arp $dev/$ip -> $mac on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for arp clean)"
        elif fault_present; then
            $HCCN -arp -d dev "$dev" ip "$ip" || { echo "arp del failed" >&2; exit 1; }
            sidecar_clear rNPU_arp_poison "$chip"
            echo "removed poisoned arp $dev/$ip on chip $chip"
        else echo "arp entry not present, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -arp -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
