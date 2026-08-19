#!/bin/sh
# rNPU_aic_load: AICore stress via torch_npu matmul.
# inject: run _npu_stress.py aicore in background, write pidfile
# clean:  kill stress process
# query:  npu-smi info -t usages (check Aicore Usage Rate)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_aic_load-$chip.pid"
STRESS_PY="$(cd "$(dirname "$0")" && pwd)/_npu_stress.py"

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        if [ ! -f "$STRESS_PY" ]; then
            echo "ERROR: _npu_stress.py not found" >&2; exit 1
        fi
        dev_id=$(npu_acl_dev_id "$chip")
        [ -z "$dev_id" ] && { echo "cannot find ACL dev id for chip $chip (dev-map missing?)" >&2; exit 1; }
        load_pct=${DCAT_PARAM_LOAD_PCT:-100}
        python3 "$STRESS_PY" aicore "$dev_id" 0 "$load_pct" 0 >/dev/null 2>&1 &
        echo $! > "$SIDECAR"
        sleep 2
        if ! kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            rm -f "$SIDECAR"
            echo "AICore stress failed: cannot start on chip $chip (torch_npu missing? HBM insufficient?)" >&2
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
                card_chip=$(npu_phy_to_card "$c"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
                usages=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null)
                ai_pct=$(echo "$usages" | awk '/Aicube/{print $NF}')
                [ -z "$ai_pct" ] && ai_pct=$(echo "$usages" | awk '/Aicore/{print $NF}')
                echo "  AICore Usage(%): ${ai_pct:-?}"
                found=1
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE: no AICore stress"; exit 1; }
        elif [ -f "$SIDECAR" ] && kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            echo "FAULT CONFIRMED: AICore stress active (pid $(cat $SIDECAR))"
            card_chip=$(npu_phy_to_card "$chip"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
            usages=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null)
            ai_pct=$(echo "$usages" | awk '/Aicube/{print $NF}')
            [ -z "$ai_pct" ] && ai_pct=$(echo "$usages" | awk '/Aicore/{print $NF}')
            echo "AICore Usage(%): ${ai_pct:-?}"
            exit 0
        else
            rm -f "$SIDECAR" 2>/dev/null
            echo "FAULT NOT ACTIVE: no AICore stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
