#!/bin/sh
# rNPU_hbm_fault: HBM stress via ACL malloc+memset (no torch_npu required).
# inject: run _npu_stress hbm in background, write sidecar
# clean:  kill stress process
# query:  npu-smi info -t usages (check HBM Usage Rate)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_hbm_fault-$chip.pid"
STRESS_BIN="$(cd "$(dirname "$0")/../../.." && pwd)/build/_npu_stress"
DEV_MAP_FILE="/tmp/dcat-npu-dev-map"

npu_acl_dev_id() {
    if [ -f "$DEV_MAP_FILE" ]; then
        awk -v card="$1" '$1==card{print $2; exit}' "$DEV_MAP_FILE"
    fi
}

# parse size string to MB: 2G=2048, 500M=500, 500=500
size_to_mb() {
    s=$1
    case "$s" in
        *[0-9]G|*[0-9]g) num=${s%[Gg]}; echo $((num * 1024));;
        *[0-9]M|*[0-9]m) num=${s%[Mm]}; echo "$num";;
        *[0-9]) echo "$s";;
        *) return 1;;
    esac
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
        size_raw=${DCAT_PARAM_SIZE:?missing required param: size}
        size_mb=$(size_to_mb "$size_raw") || { echo "invalid size: $size_raw (use 500M, 2G, 500)" >&2; exit 1; }
        "$STRESS_BIN" hbm "$dev_id" 0 "$size_mb" >/dev/null 2>&1 &
        echo $! > "$SIDECAR"
        echo "HBM stress started on chip $chip (acl dev $dev_id, pid $!, ${size_mb}MB)"
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            kill -9 $(cat "$SIDECAR") 2>/dev/null
            rm -f "$SIDECAR"
            echo "HBM stress stopped on chip $chip"
        else
            echo "no active HBM stress on chip $chip"
        fi
        ;;
    query)
        if [ -f "$SIDECAR" ]; then
            echo "FAULT CONFIRMED: HBM stress active (pid $(cat $SIDECAR))"
            npu-smi info -t usages -i "$chip" -c 0 2>/dev/null | grep -E 'HBM'
            exit 0
        else
            echo "FAULT NOT ACTIVE: no HBM stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
