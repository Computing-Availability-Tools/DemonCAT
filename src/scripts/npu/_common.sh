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
