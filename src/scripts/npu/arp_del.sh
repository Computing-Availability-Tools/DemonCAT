#!/bin/sh
# rNPU_arp_del: delete ARP entry. Clean = re-add with original mac from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
dev=${DCAT_PARAM_DEV:?missing required param: dev}
ip=${DCAT_PARAM_IP:?missing required param: ip}
HCCN="hccn_tool -i $chip"

fault_present() { ! $HCCN -arp -g 2>/dev/null | grep -Fq "$ip"; }

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        orig_mac=$($HCCN -arp -g 2>/dev/null | grep "$ip" | grep -oE 'mac [0-9a-f:]+' | awk '{print $2}')
        [ -n "$orig_mac" ] && sidecar_save rNPU_arp_del "$chip" "$orig_mac"
        $HCCN -arp -d dev "$dev" ip "$ip" || { echo "arp del failed" >&2; exit 1; }
        echo "deleted arp $dev/$ip on chip $chip (was mac $orig_mac)"
        ;;
    clean)
        if fault_present; then
            orig_mac=$(sidecar_load rNPU_arp_del "$chip"); orig_mac=${orig_mac:-00:00:00:00:00:00}
            $HCCN -arp -a dev "$dev" ip "$ip" mac "$orig_mac" || { echo "arp re-add failed" >&2; exit 1; }
            sidecar_clear rNPU_arp_del "$chip"
            echo "restored arp $dev/$ip -> $orig_mac on chip $chip"
        else echo "arp entry already present, no-op"; fi
        ;;
    query) $HCCN -arp -g; fault_present ;;
esac
