#!/bin/sh
# rNPU_chip_reset: reset NPU chip via npu-smi (service disruption).
# npu_id = NPU card ID (0-7). core = chip within card (0/1, default 0).
#
# WARNING: On multi-chip cards (e.g. 910C, 2 chips per card), resetting one
# chip may cause the entire card to reset, affecting all chips on that card.
# Use with caution in production environments.
#
# inject: npu-smi set -t reset -i <npu_id> -c <core>
# clean:  no-op (chip auto-recovers after reset)
# query:  npu-smi info (check Health column)
. "$(dirname "$0")/_common.sh"
npu_id=${DCAT_PARAM_NPU_ID:-}
if [ -n "$npu_id" ]; then npu_validate_chip "$npu_id" || { echo "npu_id validation failed" >&2; exit 1; }; fi
core=${DCAT_PARAM_CORE:-0}
SIDECAR="/tmp/dcat-rNPU_chip_reset-$npu_id-$core.bak"

case "${DCAT_OP:-inject}" in
    inject)
        : ${npu_id:?missing required param: npu_id}
        printf 'y\n' | npu-smi set -t reset -i "$npu_id" -c "$core" 2>/dev/null \
            || { echo "chip reset failed (need root / npu-smi)" >&2; exit 1; }
        printf 'reset\n' > "$SIDECAR"
        echo "reset NPU card $npu_id chip $core via npu-smi"
        ;;
    clean)
        rm -f "$SIDECAR" 2>/dev/null
        echo "chip reset state cleared on card $npu_id chip $core"
        ;;
    query)
        if [ -z "$npu_id" ]; then
            found=0
            for f in /tmp/dcat-rNPU_chip_reset-*.bak; do
                [ -f "$f" ] || continue
                found=1
                base=$(basename "$f")
                id_part=${base#dcat-rNPU_chip_reset-}
                id_part=${id_part%.bak}
                sid=${id_part%-*}
                sc=${id_part##*-}
                echo "FAULT ACTIVE: chip reset was issued on card $sid chip $sc"
                npu-smi info 2>/dev/null | grep -A2 "^| $sid " | head -3
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE"; exit 1; }
        elif [ -f "$SIDECAR" ]; then
            echo "FAULT ACTIVE: chip reset was issued on card $npu_id chip $core"
            npu-smi info 2>/dev/null | grep -A2 "^| $npu_id " | head -3
            exit 0
        else
            npu-smi info 2>/dev/null | grep -A2 "^| $npu_id " | head -3
            echo "FAULT NOT ACTIVE"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
