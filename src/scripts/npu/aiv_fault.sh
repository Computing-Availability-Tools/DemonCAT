#!/bin/sh
# rNPU_aiv_fault: AIVector stress via ACL d2d memcpy (no torch_npu required).
# inject: run _npu_stress aivector in background, write sidecar
# clean:  kill stress process
# query:  npu-smi info -t usages (check Aivector Usage Rate)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_aiv_fault-$chip.pid"
STRESS_BIN="$(cd "$(dirname "$0")/../../.." && pwd)/build/_npu_stress"
DEV_MAP_FILE="/tmp/dcat-npu-dev-map"

npu_acl_dev_id() {
    if [ -f "$DEV_MAP_FILE" ]; then
        awk -v card="$1" '$1==card{print $2; exit}' "$DEV_MAP_FILE"
    fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        if [ ! -x "$STRESS_BIN" ]; then
            echo "ERROR: _npu_stress not built. Run: cd build && cmake .. && make _npu_stress" >&2
            exit 1
        fi
        dev_id=$(npu_acl_dev_id "$chip")
        [ -z "$dev_id" ] && dev_id=0
        duration=${DCAT_PARAM_DURATION:-30}
        "$STRESS_BIN" aivector "$dev_id" "$duration" 512 >/dev/null 2>&1 &
        echo $! > "$SIDECAR"
        echo "AIVector stress started on chip $chip (acl dev $dev_id, pid $!, ${duration}s)"
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            kill -9 $(cat "$SIDECAR") 2>/dev/null
            rm -f "$SIDECAR"
            echo "AIVector stress stopped on chip $chip"
        else
            echo "no active AIVector stress on chip $chip"
        fi
        ;;
    query)
        if [ -f "$SIDECAR" ]; then
            echo "FAULT CONFIRMED: AIVector stress active (pid $(cat $SIDECAR))"
            npu-smi info -t usages -i "$chip" -c 0 2>/dev/null | grep -E 'Aicore|Aivector'
            exit 0
        else
            echo "FAULT NOT ACTIVE: no AIVector stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
