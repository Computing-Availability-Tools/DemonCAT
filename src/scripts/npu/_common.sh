# shellcheck shell=bash
# _common.sh — npu module shared helpers. Sourced by rNPU_*.sh scripts.

# Prefer user's ASCEND_OPP_PATH (matches runtime CANN); fall back to toolkit default
if [ -z "${ASCEND_OPP_PATH:-}" ]; then
    _TK_OPP="/usr/local/Ascend/ascend-toolkit/latest/opp"
    [ -d "$_TK_OPP" ] && export ASCEND_OPP_PATH="$_TK_OPP"
fi

DEV_MAP_FILE="/tmp/dcat-npu-dev-map"

npu_check_env() {
    command -v hccn_tool >/dev/null 2>&1 || { echo "hccn_tool not found in PATH" >&2; exit 1; }
}

npu_validate_chip() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
    esac
    return 0
}

# Generate /tmp/dcat-npu-dev-map if missing.
# Format: <Phy-ID> <ACL-dev-id>
# ACL dev ID = sequential index of Phy-IDs (numerically sorted)
npu_gen_dev_map() {
    [ -f "$DEV_MAP_FILE" ] && return 0
    local acl_id=0
    for phy_id in $(ls /dev/davinci[0-9]* 2>/dev/null | sed 's|/dev/davinci||;s/[^0-9].*//' | sort -n); do
        echo "$phy_id $acl_id" >> "$DEV_MAP_FILE"
        acl_id=$((acl_id + 1))
    done
    [ -f "$DEV_MAP_FILE" ] || return 1
}

# Lookup ACL device ID from Phy-ID (auto-generate map if missing)
npu_acl_dev_id() {
    [ ! -f "$DEV_MAP_FILE" ] && npu_gen_dev_map
    awk -v phy="$1" '$1==phy{print $2; exit}' "$DEV_MAP_FILE" 2>/dev/null
}

# Get PCIe BDF from Phy-ID via devdrv driver sysfs.
# devdrv driver's dev_id attribute = Phy-ID (exact mapping, no sorting needed).
npu_phy_to_bdf() {
    local phy_id="$1" bdf
    for bdf in /sys/bus/pci/drivers/devdrv_device_driver/0000:*; do
        [ -e "$bdf" ] || continue
        bdf=$(basename "$bdf")
        if [ "$(cat "/sys/bus/pci/drivers/devdrv_device_driver/$bdf/dev_id" 2>/dev/null)" = "$phy_id" ]; then
            echo "$bdf"
            return 0
        fi
    done
    return 1
}

# Convert Phy-ID to "<npu_card_id> <chip_within_card>" for npu-smi -i -c queries.
# Auto-detects chips-per-card from /dev/davinci count vs unique NPU card count.
npu_phy_to_card() {
    local phy_id="$1" n_davinci n_cards per_card
    n_davinci=$(ls /dev/davinci[0-9]* 2>/dev/null | wc -l)
    # Count unique NPU card IDs from npu-smi info first column (deduplicated)
    n_cards=$(npu-smi info 2>/dev/null | grep -oE '^\| [0-9]+' | awk '{print $2}' | sort -nu | wc -l)
    if [ "$n_davinci" -gt 0 ] && [ "$n_cards" -gt 0 ]; then
        per_card=$((n_davinci / n_cards))
        if [ "$per_card" -ge 1 ]; then
            echo "$((phy_id / per_card)) $((phy_id % per_card))"
            return 0
        fi
    fi
    # Fallback: 1 chip per card (910B4 where Phy-ID = NPU card ID)
    echo "$phy_id 0"
}

# List all valid NPU Phy-IDs (from /dev/davinci* or DCAT_NPU_CHIPS if set)
# DCAT_NPU_CHIPS="0,1,2,3" 可指定允许使用的芯片范围
npu_list_chips() {
    if [ -n "$DCAT_NPU_CHIPS" ]; then
        # 用户指定范围：支持逗号分隔或连字符范围
        echo "$DCAT_NPU_CHIPS" | tr ',' '\n' | while read -r c; do
            case "$c" in
                *-*)
                    start=${c%%-*}; end=${c##*-}
                    case "$start" in *[!0-9]*|"") continue;; esac
                    case "$end"   in *[!0-9]*|"") continue;; esac
                    n=$start; while [ "$n" -le "$end" ]; do
                        hccn_tool -i "$n" -link -g 2>/dev/null | grep -q 'link' && echo "$n"
                        n=$((n + 1))
                    done
                    ;;
                *)
                    case "$c" in *[!0-9]*|"") continue;; esac
                    hccn_tool -i "$c" -link -g 2>/dev/null | grep -q 'link' && echo "$c"
                    ;;
            esac
        done
    else
        # 从 /dev/davinci* 设备文件动态获取 chip 列表（不硬编码数量）
        for d in /dev/davinci[0-9]*; do
            [ -e "$d" ] || continue
            basename "$d" | grep -oE '[0-9]+$'
        done | sort -n
    fi
}

# Execute callback for each chip: chip has value → once; chip empty → all devices
# Exit code: 0 if any chip confirms fault, 1 if none (for dispatch confirmed flag)
# Usage: npu_foreach_chip 'command with $HCCN'
npu_foreach_chip() {
    if [ -n "$chip" ]; then
        eval "$1"
    else
        _rc=1
        for c in $(npu_list_chips); do
            echo "=== chip $c ==="
            _oc=$chip; _oh=$HCCN
            chip=$c; HCCN="hccn_tool -i $c"
            eval "$1" && _rc=0
            chip=$_oc; HCCN=$_oh
        done
        return $_rc
    fi
}

# sidecar_save <uid> <chip> <value>
sidecar_save() {
    printf '%s\n' "$3" > "/tmp/dcat-$1-$2.bak"
}
# sidecar_load <uid> <chip>  (echoes value, empty if missing)
sidecar_load() {
    cat "/tmp/dcat-$1-$2.bak" 2>/dev/null
}
# sidecar_clear <uid> <chip>
sidecar_clear() {
    rm -f "/tmp/dcat-$1-$2.bak" 2>/dev/null
}
