#!/bin/sh
# rNPU_arp_poison: add wrong ARP entry. Clean = delete the added entry.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
dev=${DCAT_PARAM_DEV:?missing required param: dev}
ip=${DCAT_PARAM_IP:?missing required param: ip}
mac=${DCAT_PARAM_MAC:?missing required param: mac}
HCCN="hccn_tool -i $chip"

fault_present() { $HCCN -arp -g 2>/dev/null | grep -F "$ip" | grep -Fq "$mac"; }

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        $HCCN -arp -a dev "$dev" ip "$ip" mac "$mac" || { echo "arp add failed" >&2; exit 1; }
        fault_present || { echo "rNPU_arp_poison 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "poisoned arp $dev/$ip -> $mac on chip $chip"
        ;;
    clean)
        if fault_present; then
            $HCCN -arp -d dev "$dev" ip "$ip" || { echo "arp del failed" >&2; exit 1; }
            echo "removed poisoned arp $dev/$ip on chip $chip"
        else echo "arp entry not present, no-op"; fi
        ;;
    query) $HCCN -arp -g; fault_present ;;
esac
