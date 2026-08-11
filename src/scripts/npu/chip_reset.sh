#!/bin/sh
# rNPU_chip_reset: reset NPU chip via npu-smi (service disruption).
# inject: npu-smi set -t reset -i <chip> -c 0
# clean:  no-op (chip auto-recovers after reset; check health)
# query:  npu-smi info (check Health column)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
SIDECAR="/tmp/dcat-rNPU_chip_reset-$chip.bak"

case "${DCAT_OP:-inject}" in
    inject)
        npu-smi set -t reset -i "$chip" -c 0 -y 2>/dev/null \
            || npu-smi set -t reset -i "$chip" -c 0 2>/dev/null \
            || { echo "chip reset failed (need root / npu-smi)" >&2; exit 1; }
        printf 'reset\n' > "$SIDECAR"
        echo "reset NPU chip $chip via npu-smi"
        ;;
    clean)
        # chip auto-recovers after reset; just clear sidecar
        rm -f "$SIDECAR" 2>/dev/null
        # optionally force config recovery
        hccn_tool -i "$chip" -cfg recovery 2>/dev/null || true
        echo "chip $chip config recovered"
        ;;
    query)
        npu-smi info 2>/dev/null | grep -A2 "^| $chip "
        if [ -f "$SIDECAR" ]; then exit 0; else exit 1; fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
