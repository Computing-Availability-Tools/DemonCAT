#!/bin/sh
# rNPU_aic_fault: AICore stress via ACL d2d memcpy (no torch_npu required).
# inject: run _npu_stress aicore in background, write sidecar
# clean:  kill stress process
# query:  npu-smi info -t usages (check Aicore Usage Rate)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_aic_fault-$chip.pid"
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
        "$STRESS_BIN" aicore "$dev_id" 0 512 >/dev/null 2>&1 &
        echo $! > "$SIDECAR"
        sleep 1
        if ! kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            rm -f "$SIDECAR"
            echo "AICore stress failed: cannot start on chip $chip (HBM insufficient?)" >&2
            exit 1
        fi
        echo "AICore stress started on chip $chip (dev $dev_id, pid $!)"
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            kill -9 $(cat "$SIDECAR") 2>/dev/null
            rm -f "$SIDECAR"
            echo "AICore stress stopped on chip $chip"
        else
            echo "no active AICore stress on chip $chip"
        fi
        ;;
    query)
        if [ -f "$SIDECAR" ] && kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            echo "FAULT CONFIRMED: AICore stress active (pid $(cat $SIDECAR))"
            ai_pct=$(npu-smi info -t usages -i "$chip" -c 0 2>/dev/null | awk '/Aicore/{print $NF}')
            hbm_raw=$(npu-smi info 2>/dev/null | grep -A1 "^| $chip " | tail -1 | awk -F'|' '{gsub(/^ +| +$/,"",$5); print $5}')
            echo "AICore Usage(%): ${ai_pct:-?}"
            echo "HBM Usage(MB): ${hbm_raw:-?}"
            exit 0
        else
            rm -f "$SIDECAR" 2>/dev/null
            echo "FAULT NOT ACTIVE: no AICore stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
