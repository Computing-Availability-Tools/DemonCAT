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
        pid=$!
        echo "$pid" > "$SIDECAR"
        sleep 2
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$SIDECAR"
            echo "HBM stress failed: cannot allocate ${size_mb}MB on chip $chip (HBM insufficient?)" >&2
            exit 1
        fi
        proc_mem=$(npu-smi info 2>/dev/null | grep '_npu_stress' | awk -F'|' '{gsub(/^ +| +$/,"",$5); print $5}')
        if [ -n "$proc_mem" ] && [ "$proc_mem" -lt $((size_mb / 2)) ] 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null
            rm -f "$SIDECAR"
            echo "HBM stress failed: only ${proc_mem}MB allocated (requested ${size_mb}MB), HBM insufficient" >&2
            exit 1
        fi
        echo "HBM stress started on chip $chip (dev $dev_id, pid $pid, ${size_mb}MB)"
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
        if [ -z "$chip" ]; then
            found=0
            for f in /tmp/dcat-rNPU_hbm_fault-*.pid; do
                [ -f "$f" ] || continue
                c=$(echo "$f" | sed 's/.*-//;s/\.pid//')
                pid=$(cat "$f" 2>/dev/null)
                kill -0 "$pid" 2>/dev/null || { rm -f "$f"; continue; }
                echo "FAULT CONFIRMED: HBM stress active on chip $c (pid $pid)"
                hbm_raw=$(npu-smi info 2>/dev/null | awk "/^\\| $c /{getline;print}" | grep -oE '[0-9]+ */ *[0-9]+' | tail -1)
                echo "  HBM Usage: ${hbm_raw:-?}"
                found=1
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE: no HBM stress"; exit 1; }
        elif [ -f "$SIDECAR" ] && kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            echo "FAULT CONFIRMED: HBM stress active (pid $(cat $SIDECAR))"
            hbm_pct=$(npu-smi info -t usages -i "$chip" -c 0 2>/dev/null | awk '/HBM Usage Rate/{print $NF}')
            hbm_raw=$(npu-smi info 2>/dev/null | awk "/^\\| $chip /{getline;print}" | grep -oE '[0-9]+ */ *[0-9]+' | tail -1)
            echo "HBM Usage: ${hbm_raw:-?} (${hbm_pct:-?}%)"
            npu-smi info 2>/dev/null | grep '_npu_stress' | awk -F'|' '{gsub(/^ +| +$/,"",$4); gsub(/^ +| +$/,"",$5); print $4": "$5"MB"}'
            exit 0
        else
            rm -f "$SIDECAR" 2>/dev/null
            echo "FAULT NOT ACTIVE: no HBM stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
