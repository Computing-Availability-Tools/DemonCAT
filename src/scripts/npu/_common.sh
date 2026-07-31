# _common.sh — npu module shared helpers. Sourced by rNPU_*.sh scripts.

npu_check_env() {
    command -v hccn_tool >/dev/null 2>&1 || { echo "hccn_tool not found in PATH" >&2; exit 1; }
}

npu_validate_chip() {
    case "$1" in
        [0-9]) return 0 ;;
        '') return 1 ;;
        *) echo "chip must be a single digit 0-9, got: '$1'" >&2; return 1 ;;
    esac
}

# List all valid NPU device IDs (0-9 with link status readable)
npu_list_chips() {
    for c in 0 1 2 3 4 5 6 7 8 9; do
        hccn_tool -i $c -link -g 2>/dev/null | grep -q 'link' && echo "$c"
    done
}

# Execute callback for each chip: chip has value → once; chip empty → all devices
# Usage: npu_foreach_chip 'command with $HCCN'
npu_foreach_chip() {
    if [ -n "$chip" ]; then
        eval "$1"
    else
        for c in $(npu_list_chips); do
            echo "=== chip $c ==="
            _oc=$chip; _oh=$HCCN
            chip=$c; HCCN="hccn_tool -i $c"
            eval "$1"
            chip=$_oc; HCCN=$_oh
        done
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
