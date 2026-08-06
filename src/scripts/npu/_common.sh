# _common.sh — npu module shared helpers. Sourced by rNPU_*.sh scripts.

npu_check_env() {
    command -v hccn_tool >/dev/null 2>&1 || { echo "hccn_tool not found in PATH" >&2; exit 1; }
}

npu_validate_chip() {
    case "$1" in
        [0-9]|10|11) return 0 ;;
        '') return 1 ;;
        *) echo "chip must be 0-11, got: '$1'" >&2; return 1 ;;
    esac
}

# List all valid NPU device IDs (0-11, or DCAT_NPU_CHIPS if set)
# DCAT_NPU_CHIPS="0,1,2,3" 可指定允许使用的芯片范围
npu_list_chips() {
    if [ -n "$DCAT_NPU_CHIPS" ]; then
        # 用户指定范围：支持逗号分隔或连字符范围
        echo "$DCAT_NPU_CHIPS" | tr ',' '\n' | while read -r c; do
            case "$c" in
                *-*)
                    start=${c%%-*}; end=${c##*-}
                    n=$start; while [ "$n" -le "$end" ]; do
                        hccn_tool -i $n -link -g 2>/dev/null | grep -q 'link' && echo "$n"
                        n=$((n + 1))
                    done
                    ;;
                *)
                    hccn_tool -i $c -link -g 2>/dev/null | grep -q 'link' && echo "$c"
                    ;;
            esac
        done
    else
        # 默认探测所有 0-11
        for c in 0 1 2 3 4 5 6 7 8 9 10 11; do
            hccn_tool -i $c -link -g 2>/dev/null | grep -q 'link' && echo "$c"
        done
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
