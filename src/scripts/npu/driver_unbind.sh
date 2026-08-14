#!/bin/sh
# rNPU_driver_unbind: unbind NPU from devdrv driver (simulate driver abnormality).
# chip = Phy-ID (0-15). Uses npu_phy_to_bdf for PCIe address lookup.
# inject: echo <pcie_addr> > /sys/bus/pci/drivers/devdrv_device_driver/unbind
# clean:  echo <pcie_addr> > /sys/bus/pci/drivers/devdrv_device_driver/bind + FLR reset
# query:  check if device is bound to driver
# NOTE:   On 910B4, driver rebind restores the driver binding but the NPU firmware
#         may not fully recover. A warm reboot (reboot) restores the NPU.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
DRIVER="/sys/bus/pci/drivers/devdrv_device_driver"
SIDECAR="/tmp/dcat-rNPU_driver_unbind-$chip.bak"

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        addr=$(npu_phy_to_bdf "$chip")
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
        if [ -z "$chip" ]; then
            found=0
            for f in /tmp/dcat-rNPU_driver_unbind-*.bak; do
                [ -f "$f" ] || continue
                found=1
                addr=$(cat "$f")
                c=$(basename "$f" | sed 's/.*-//;s/\.bak//')
                echo "FAULT CONFIRMED: driver unbind active on chip $c (pcie $addr)"
                ls -l "/sys/bus/pci/drivers/devdrv_device_driver/$addr" 2>/dev/null \
                    && echo "  still bound (unexpected)" \
                    || echo "  unbound (expected)"
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE: no driver unbind"; exit 1; }
        elif [ -f "$SIDECAR" ]; then
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
