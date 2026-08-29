#!/bin/sh
# rNPU_arp: ARP entry poisoning (inject=add, clean=del).
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
dev=${DCAT_PARAM_DEV:-}
ip=${DCAT_PARAM_IP:-}
mac=${DCAT_PARAM_MAC:-}
HCCN="$HCCN_TO hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_arp-$chip.bak"

fault_present() {
    case "${DCAT_OP:-inject}" in
        clean) ! $HCCN -arp -g 2>/dev/null | grep -Fq "$ip" ;;
        *)     $HCCN -arp -g 2>/dev/null | grep -F "$ip" | grep -Fq "$mac" ;;
    esac
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${dev:?missing required param: dev}
        : ${ip:?missing required param: ip}
        : ${mac:?missing required param: mac}
        npu_check_env
        $HCCN -arp -a dev "$dev" ip "$ip" mac "$mac" || { echo "arp add failed" >&2; exit 1; }
        printf 'dev=%s\nip=%s\n' "$dev" "$ip" > "$SIDECAR"
        fault_present || { echo "rNPU_arp 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "poisoned arp $dev/$ip -> $mac on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0; failed=0
            for bak in /tmp/dcat-rNPU_arp-*.bak; do
                [ -f "$bak" ] || continue
                cleaned=1
                c=${bak##*/dcat-rNPU_arp-}; c=${c%.bak}
                d=$(grep '^dev=' "$bak" 2>/dev/null | cut -d= -f2-)
                i=$(grep '^ip=' "$bak" 2>/dev/null | cut -d= -f2-)
                if DCAT_OP=clean DCAT_PARAM_CHIP="$c" DCAT_PARAM_DEV="$d" DCAT_PARAM_IP="$i" "$0" >/dev/null 2>&1; then :
                else echo "clean failed for chip $c" >&2; failed=1; fi
            done
            [ "$failed" = 1 ] && { echo "arp: some cleans failed (state preserved)" >&2; exit 1; }
            [ "$cleaned" = 1 ] && echo "cleaned arp (all chips)" || echo "cleaned arp (no active injection)"
        else
            : ${dev:?missing required param: dev}
            : ${ip:?missing required param: ip}
            $HCCN -arp -d dev "$dev" ip "$ip" || { echo "arp del failed" >&2; exit 1; }
            fault_present || { echo "rNPU_arp 清除回读校验失败:动作未生效" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "removed arp $dev/$ip on chip $chip"
        fi
        ;;
    query)
        if [ -n "$ip$dev" ]; then
            npu_foreach_chip '$HCCN -arp -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }'
        else
            npu_query_noargs rNPU_arp '$HCCN -arp -g 2>/dev/null | grep -Fq "$ip"'
        fi
        ;;
esac
