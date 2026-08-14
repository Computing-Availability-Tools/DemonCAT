#!/bin/sh
# rNPU_pcie_down: PCIe link speed downgrade.
# Reduces PCIe link speed from Gen4 (16GT/s) to Gen1 (2.5GT/s) by default,
# cutting PCIe bandwidth ~6.4x. NPU remains accessible at lower speed.
#
# inject: set Target Link Speed on both root port and endpoint, retrain link
# clean:  restore original LnkCtl2 values, retrain link
# query:  show current link speed, compare to original
. "$(dirname "$0")/_common.sh"
npu_id=${DCAT_PARAM_NPU_ID:-}
if [ -n "$npu_id" ]; then npu_validate_chip "$npu_id" || { echo "npu_id validation failed" >&2; exit 1; }; fi
gen=${DCAT_PARAM_GEN:-1}
# valid range: 1-3 (Gen4=original, Gen5=unsupported on 910B4)
case "$gen" in 1|2|3) ;; *) echo "gen must be 1-3 (1=Gen1 2.5GT/s, 2=Gen2 5GT/s, 3=Gen3 8GT/s)" >&2; exit 1 ;; esac
SIDECAR="/tmp/dcat-rNPU_pcie_down-$npu_id.bak"

# PCIe speed by generation
gen_speed() { case "$1" in 1) echo "2.5";; 2) echo "5";; 3) echo "8";; *) echo "2.5";; esac; }

# Find NPU PCIe BDF from npu-smi
get_npu_bdf() {
    npu-smi info -t board -i "$1" 2>/dev/null | grep 'PCIe Bus Info' | awk -F': ' '{print $2}' | tr -d ' '
}

# Find upstream root port BDF (parent in sysfs)
get_parent_bdf() {
    local bdf="$1"
    local p=$(readlink -f "/sys/bus/pci/devices/$bdf/.." 2>/dev/null)
    basename "$p"
}

# Read Express capability offset from config space (byte at 0x34)
get_cap_off() {
    setpci -s "$1" 34.B 2>/dev/null
}

# Read LnkCtl2 (4-byte dword at cap+0x30)
read_lnkctl2() {
    local bdf="$1" cap="$2"
    local off
    off=$(printf '%x' $((0x$cap + 0x30)))
    setpci -s "$bdf" "${off}.L" 2>/dev/null
}

# Write LnkCtl2
write_lnkctl2() {
    local bdf="$1" cap="$2" val="$3"
    local off
    off=$(printf '%x' $((0x$cap + 0x30)))
    setpci -s "$bdf" "${off}.L=$val" 2>/dev/null
}

# Retrain PCIe link (set bit 5 in LnkCtl at cap+0x10)
retrain_link() {
    local bdf="$1" cap="$2"
    local off cur new
    off=$(printf '%x' $((0x$cap + 0x10)))
    cur=$(setpci -s "$bdf" "${off}.W" 2>/dev/null)
    new=$(printf '%04x' $((0x$cur | 0x0020)))
    setpci -s "$bdf" "${off}.W=$new" 2>/dev/null
}

# Get current link speed string from lspci
get_link_speed() {
    lspci -s "$1" -vvv 2>/dev/null | grep 'LnkSta:' | head -1 | grep -oE 'Speed [0-9.]+GT/s'
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${npu_id:?missing required param: npu_id}
        command -v setpci >/dev/null 2>&1 || { echo "setpci not found (apt install pciutils)" >&2; exit 1; }

        npu_bdf=$(get_npu_bdf "$npu_id")
        [ -z "$npu_bdf" ] && { echo "cannot find PCIe BDF for npu_id $npu_id" >&2; exit 1; }
        parent_bdf=$(get_parent_bdf "$npu_bdf")
        [ -z "$parent_bdf" ] && { echo "cannot find upstream root port for $npu_bdf" >&2; exit 1; }

        cap_n=$(get_cap_off "$npu_bdf")
        cap_p=$(get_cap_off "$parent_bdf")

        # Save original LnkCtl2 values
        orig_n=$(read_lnkctl2 "$npu_bdf" "$cap_n")
        orig_p=$(read_lnkctl2 "$parent_bdf" "$cap_p")
        orig_speed=$(get_link_speed "$npu_bdf")

        # Set Target Link Speed to $gen on both sides
        new_n=$(printf '%08x' $(( (0x$orig_n & 0xFFFFFFF0) | gen )))
        new_p=$(printf '%08x' $(( (0x$orig_p & 0xFFFFFFF0) | gen )))
        write_lnkctl2 "$npu_bdf" "$cap_n" "$new_n"
        write_lnkctl2 "$parent_bdf" "$cap_p" "$new_p"

        # Retrain from root port
        retrain_link "$parent_bdf" "$cap_p"
        sleep 3

        cur_speed=$(get_link_speed "$npu_bdf")
        target_speed="$(gen_speed $gen)GT/s"
        echo "card $npu_id PCIe: $npu_bdf (parent $parent_bdf)"
        echo "original: $orig_speed -> target: Gen$gen ($target_speed)"
        echo "current:  $cur_speed"

        # Save state for clean
        echo "${npu_bdf}|${parent_bdf}|${cap_n}|${cap_p}|${orig_n}|${orig_p}|${orig_speed}" > "$SIDECAR"

        case "$cur_speed" in
            *"$target_speed"*) echo "OK: link downgraded to Gen$gen" ;;
            *) echo "WARNING: link speed did not change (may need cold boot)" >&2 ;;
        esac
        ;;
    clean)
        [ -f "$SIDECAR" ] || { echo "no pcie_down state for npu_id $npu_id" >&2; exit 1; }
        state=$(cat "$SIDECAR")
        npu_bdf=${state%%|*}; rest=${state#*|}
        parent_bdf=${rest%%|*}; rest=${rest#*|}
        cap_n=${rest%%|*}; rest=${rest#*|}
        cap_p=${rest%%|*}; rest=${rest#*|}
        orig_n=${rest%%|*}; rest=${rest#*|}
        orig_p=${rest%%|*}; rest=${rest#*|}
        orig_speed=${rest}

        # Restore original LnkCtl2
        write_lnkctl2 "$npu_bdf" "$cap_n" "$orig_n"
        write_lnkctl2 "$parent_bdf" "$cap_p" "$orig_p"

        # Retrain from root port
        retrain_link "$parent_bdf" "$cap_p"
        sleep 3

        cur_speed=$(get_link_speed "$npu_bdf")
        echo "restored: $cur_speed (original was $orig_speed)"
        rm -f "$SIDECAR"
        ;;
    query)
        if [ -z "$npu_id" ]; then
            found=0
            for f in /tmp/dcat-rNPU_pcie_down-*.bak; do
                [ -f "$f" ] || continue
                state=$(cat "$f")
                bdf=${state%%:*}
                cur_speed=$(get_link_speed "$bdf")
                sid=$(basename "$f" | sed 's/.*-//;s/\.bak//')
                echo "FAULT ACTIVE: PCIe link downgraded on npu $sid"
                echo "  bdf=$bdf current: $cur_speed"
                found=1
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE"; exit 1; }
        elif [ -f "$SIDECAR" ]; then
            state=$(cat "$SIDECAR")
            npu_bdf=${state%%:*}
            cur_speed=$(get_link_speed "$npu_bdf")
            echo "FAULT ACTIVE: PCIe link downgraded"
            echo "current: $cur_speed"
            exit 0
        else
            if [ -n "$npu_id" ]; then
                npu_bdf=$(get_npu_bdf "$npu_id")
                [ -n "$npu_bdf" ] && echo "normal: $(get_link_speed "$npu_bdf")" && exit 1
            fi
            echo "FAULT NOT ACTIVE"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
