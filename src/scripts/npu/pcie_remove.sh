#!/bin/sh
# rNPU_pcie_remove: remove NPU from PCIe bus (simulate NPU card loss).
# inject: echo 1 > /sys/bus/pci/devices/<pcie_addr>/remove
# clean:  echo 1 > /sys/bus/pci/rescan + FLR reset
# query:  check if NPU device still exists
# NOTE:   On 910B4, PCIe rescan restores the device entry but the NPU firmware
#         does not reinitialize. A cold boot (power off + power on) is required.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_pcie_remove-$chip.bak"
RESCAN="/sys/bus/pci/rescan"

npu_pcie_addr() {
    npu-smi info 2>/dev/null | awk -v chip="$1" '
        /910B/ { cur_chip=$2 }
        /0000:/ { if (cur_chip==chip) { for(i=1;i<=NF;i++) if($i ~ /0000:/) {print $i; exit} } }
    '
    # fallback: lspci + /dev/davinci* mapping (sorted by PCI addr → sorted by card id)
    if [ -z "$addr" ]; then
        pci_addrs=$(lspci -D 2>/dev/null | grep 'Processing accelerators.*d802' | awk '{print $1}' | sort)
        card_ids=$(ls /dev/davinci[0-9] 2>/dev/null | sed 's|/dev/davinci||' | sort -n)
        idx=1
        for p in $pci_addrs; do
            c=$(echo "$card_ids" | sed -n "${idx}p")
            if [ "$c" = "$1" ]; then echo "$p"; return; fi
            idx=$((idx + 1))
        done
    fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        addr=$(npu_pcie_addr "$chip")
        if [ -z "$addr" ]; then
            echo "cannot find PCIe address for chip $chip" >&2; exit 1
        fi
        printf '%s\n' "$addr" > "$SIDECAR"
        if echo 1 > "/sys/bus/pci/devices/$addr/remove" 2>/dev/null; then
            echo "PCIe remove on chip $chip (pcie $addr) — NPU card removed from bus"
        else
            echo "PCIe remove failed for $addr" >&2
            rm -f "$SIDECAR"
            exit 1
        fi
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            addr=$(cat "$SIDECAR")
            echo 1 > "$RESCAN" 2>/dev/null || true
            sleep 5
            if [ -d "/sys/bus/pci/devices/$addr" ]; then
                if [ -w "/sys/bus/pci/devices/$addr/reset" ]; then
                    echo 1 > "/sys/bus/pci/devices/$addr/reset" 2>/dev/null || true
                    sleep 2
                fi
                echo "PCIe rescan restored chip $chip (pcie $addr)"
            else
                echo "WARNING: device $addr not found after rescan — may need manual replug or reboot"
            fi
            rm -f "$SIDECAR"
        else
            echo "no active PCIe remove on chip $chip"
        fi
        ;;
    query)
        if [ -f "$SIDECAR" ]; then
            addr=$(cat "$SIDECAR")
            echo "FAULT CONFIRMED: PCIe remove active (pcie $addr)"
            if [ -d "/sys/bus/pci/devices/$addr" ]; then
                echo "device still present (unexpected)"
            else
                echo "device removed (expected)"
            fi
            exit 0
        else
            echo "FAULT NOT ACTIVE: no PCIe remove"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
