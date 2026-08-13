#!/bin/sh
# rNPU_aic_load: AICore stress via ACL d2d memcpy (no torch_npu required).
# inject: run _npu_stress aicore in background, write sidecar
# clean:  kill stress process
# query:  npu-smi info -t usages (check Aicore Usage Rate)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_aic_load-$chip.pid"
STRESS_BIN="$(cd "$(dirname "$0")/../../.." && pwd)/build/_npu_stress"

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        if [ ! -x "$STRESS_BIN" ]; then
            echo "ERROR: _npu_stress not built. Run: cd build && cmake .. && make _npu_stress" >&2
            exit 1
        fi
        dev_id=$(npu_acl_dev_id "$chip")
        [ -z "$dev_id" ] && { echo "cannot find ACL dev id for chip $chip (dev-map missing?)" >&2; exit 1; }
        load_pct=${DCAT_PARAM_LOAD_PCT:-100}
        "$STRESS_BIN" aicore "$dev_id" 0 512 "$load_pct" >/dev/null 2>&1 &
        echo $! > "$SIDECAR"
        sleep 1
        if ! kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            rm -f "$SIDECAR"
            echo "AICore stress failed: cannot start on chip $chip (HBM insufficient?)" >&2
            exit 1
        fi
        echo "AICore stress started on chip $chip (dev $dev_id, pid $!, load=${load_pct}%)"
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
        if [ -z "$chip" ]; then
            found=0
            for f in /tmp/dcat-rNPU_aic_load-*.pid; do
                [ -f "$f" ] || continue
                c=$(echo "$f" | sed 's/.*-//;s/\.pid//')
                pid=$(cat "$f" 2>/dev/null)
                kill -0 "$pid" 2>/dev/null || { rm -f "$f"; continue; }
                echo "FAULT CONFIRMED: AICore stress active on chip $c (pid $pid)"
                ai_pct=$(npu-smi info -t usages -i "$c" -c 0 2>/dev/null | awk '/Aicore/{print $NF}')
                echo "  AICore Usage(%): ${ai_pct:-?}"
                found=1
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE: no AICore stress"; exit 1; }
        elif [ -f "$SIDECAR" ] && kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            echo "FAULT CONFIRMED: AICore stress active (pid $(cat $SIDECAR))"
            ai_pct=$(npu-smi info -t usages -i "$chip" -c 0 2>/dev/null | awk '/Aicore/{print $NF}')
            hbm_raw=$(npu-smi info 2>/dev/null | awk "/^\\| $chip /{getline;print}" | grep -oE '[0-9]+ */ *[0-9]+' | tail -1)
            echo "AICore Usage(%): ${ai_pct:-?}"
            echo "HBM Usage: ${hbm_raw:-?}"
            exit 0
        else
            rm -f "$SIDECAR" 2>/dev/null
            echo "FAULT NOT ACTIVE: no AICore stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
