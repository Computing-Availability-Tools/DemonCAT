#!/bin/sh
# rNPU_driver_unbind: unbind NPU from devdrv driver (simulate driver abnormality).
# inject: echo <pcie_addr> > /sys/bus/pci/drivers/devdrv_device_driver/unbind
# clean:  echo <pcie_addr> > /sys/bus/pci/drivers/devdrv_device_driver/bind + FLR reset
# query:  check if device is bound to driver
# NOTE:   On 910B4, driver unbind/rebind restores the driver binding but NOT the
#         NPU firmware. Full recovery requires a host reboot.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
DRIVER="/sys/bus/pci/drivers/devdrv_device_driver"
SIDECAR="/tmp/dcat-rNPU_driver_unbind-$chip.bak"

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
        if echo "$addr" > "$DRIVER/unbind" 2>/dev/null; then
            echo "driver unbind on chip $chip (pcie $addr)"
        else
            echo "unbind failed for $addr (already unbound?)" >&2
            rm -f "$SIDECAR"
            exit 1
        fi
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            addr=$(cat "$SIDECAR")
            echo "$addr" > "$DRIVER/bind" 2>/dev/null || true
            sleep 1
            if [ -w "/sys/bus/pci/devices/$addr/reset" ]; then
                echo 1 > "/sys/bus/pci/devices/$addr/reset" 2>/dev/null || true
                sleep 2
            fi
            rm -f "$SIDECAR"
            echo "driver rebind on chip $chip (pcie $addr)"
        else
            echo "no active driver unbind on chip $chip"
        fi
        ;;
    query)
        if [ -f "$SIDECAR" ]; then
            addr=$(cat "$SIDECAR")
            echo "FAULT CONFIRMED: driver unbind active (pcie $addr)"
            ls -l "/sys/bus/pci/drivers/devdrv_device_driver/$addr" 2>/dev/null \
                && echo "still bound (unexpected)" \
                || echo "unbound (expected)"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no driver unbind"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
