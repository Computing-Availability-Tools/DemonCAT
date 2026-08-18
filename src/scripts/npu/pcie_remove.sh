#!/bin/sh
# rNPU_pcie_remove: remove NPU from PCIe bus (simulate NPU card loss).
# chip = Phy-ID (0-15). Uses npu_phy_to_bdf for PCIe address lookup.
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

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        addr=$(npu_phy_to_bdf "$chip")
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
        if [ -z "$chip" ]; then
            found=0
            for f in /tmp/dcat-rNPU_pcie_remove-*.bak; do
                [ -f "$f" ] || continue
                found=1
                addr=$(cat "$f")
                c=$(basename "$f" | sed 's/.*-//;s/\.bak//')
                echo "FAULT CONFIRMED: PCIe remove active on chip $c (pcie $addr)"
                if [ -d "/sys/bus/pci/devices/$addr" ]; then
                    echo "  device still present (unexpected)"
                else
                    echo "  device removed (expected)"
                fi
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE: no PCIe remove"; exit 1; }
        elif [ -f "$SIDECAR" ]; then
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
