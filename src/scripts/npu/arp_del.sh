#!/bin/sh
# rNPU_arp_del: delete ARP entry. Clean = re-add with original mac from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
dev=${DCAT_PARAM_DEV:-}
ip=${DCAT_PARAM_IP:-}
HCCN="hccn_tool -i $chip"

fault_present() { ! $HCCN -arp -g 2>/dev/null | grep -Fq "$ip"; }

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${dev:?missing required param: dev}
        : ${ip:?missing required param: ip}
        npu_check_env
        orig_mac=$($HCCN -arp -g 2>/dev/null | grep "$ip" | grep -oE 'at [0-9a-f:]+' | awk '{print $2}')
        [ -n "$orig_mac" ] && sidecar_save rNPU_arp_del "$chip" "$orig_mac"
        $HCCN -arp -d dev "$dev" ip "$ip" || { echo "arp del failed" >&2; exit 1; }
        echo "deleted arp $dev/$ip on chip $chip (was mac $orig_mac)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for arp clean)"
        elif fault_present; then
            orig_mac=$(sidecar_load rNPU_arp_del "$chip"); orig_mac=${orig_mac:-00:00:00:00:00:00}
            $HCCN -arp -a dev "$dev" ip "$ip" mac "$orig_mac" || { echo "arp re-add failed" >&2; exit 1; }
            sidecar_clear rNPU_arp_del "$chip"
            echo "restored arp $dev/$ip -> $orig_mac on chip $chip"
        else echo "arp entry already present, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -arp -g; fault_present' ;;
esac
