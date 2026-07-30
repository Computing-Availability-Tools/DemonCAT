#!/bin/sh
# rNPU_arp_poison: add wrong ARP entry. Clean = delete the added entry.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
dev=${DCAT_PARAM_DEV:-}
ip=${DCAT_PARAM_IP:-}
mac=${DCAT_PARAM_MAC:-}
HCCN="hccn_tool -i $chip"

fault_present() { $HCCN -arp -g 2>/dev/null | grep -F "$ip" | grep -Fq "$mac"; }

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${dev:?missing required param: dev}
        : ${ip:?missing required param: ip}
        : ${mac:?missing required param: mac}
        npu_check_env
        $HCCN -arp -a dev "$dev" ip "$ip" mac "$mac" || { echo "arp add failed" >&2; exit 1; }
        echo "poisoned arp $dev/$ip -> $mac on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for arp clean)"
        elif fault_present; then
            $HCCN -arp -d dev "$dev" ip "$ip" || { echo "arp del failed" >&2; exit 1; }
            echo "removed poisoned arp $dev/$ip on chip $chip"
        else echo "arp entry not present, no-op"; fi
        ;;
    query) $HCCN -arp -g; fault_present ;;
esac
