#!/bin/sh
# rNPU_aicpu_load: AICpu stress via aclnnTopk.
# inject: run _npu_stress aicpu in background, write pidfile
# clean:  kill stress process
# query:  npu-smi info -t usages (check Aicpu Usage Rate)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_aicpu_load-$chip.pid"
STRESS_BIN="$(cd "$(dirname "$0")/../../.." && pwd)/build/_npu_stress"

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        if [ ! -x "$STRESS_BIN" ]; then
            echo "ERROR: _npu_stress not built. Run: cd build && cmake .. && make _npu_stress" >&2; exit 1
        fi
        dev_id=$(npu_acl_dev_id "$chip")
        [ -z "$dev_id" ] && { echo "cannot find ACL dev id for chip $chip (dev-map missing?)" >&2; exit 1; }
        load_pct=${DCAT_PARAM_LOAD_PCT:-100}
        LOG="/tmp/dcat-rNPU_aicpu_load-$chip.log"
        "$STRESS_BIN" aicpu "$dev_id" 0 "$load_pct" 0 > "$LOG" 2>&1 &
        echo $! > "$SIDECAR"
        sleep 5
        if ! kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            rm -f "$SIDECAR"
            echo "AICpu stress failed on chip $chip:" >&2
            tail -3 "$LOG" >&2
            rm -f "$LOG"
            exit 1
        fi
        rm -f "$LOG"
        echo "AICpu stress started on chip $chip (dev $dev_id, pid $!, load=${load_pct}%)"
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            kill -9 $(cat "$SIDECAR") 2>/dev/null
            rm -f "$SIDECAR"
            echo "AICpu stress stopped on chip $chip"
        else
            echo "no active AICpu stress on chip $chip"
        fi
        ;;
    query)
        if [ -z "$chip" ]; then
            found=0
            for f in /tmp/dcat-rNPU_aicpu_load-*.pid; do
                [ -f "$f" ] || continue
                c=$(echo "$f" | sed 's/.*-//;s/\.pid//')
                pid=$(cat "$f" 2>/dev/null)
                kill -0 "$pid" 2>/dev/null || { rm -f "$f"; continue; }
                echo "FAULT CONFIRMED: AICpu stress active on chip $c (pid $pid)"
                card_chip=$(npu_phy_to_card "$c"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
                aicpu_pct=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null | awk '/Aicpu/{print $NF}')
                echo "  AICpu Usage(%): ${aicpu_pct:-?}"
                found=1
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE: no AICpu stress"; exit 1; }
        elif [ -f "$SIDECAR" ] && kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            echo "FAULT CONFIRMED: AICpu stress active (pid $(cat $SIDECAR))"
            card_chip=$(npu_phy_to_card "$chip"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
            aicpu_pct=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null | awk '/Aicpu/{print $NF}')
            echo "AICpu Usage(%): ${aicpu_pct:-?}"
            exit 0
        else
            rm -f "$SIDECAR" 2>/dev/null
            echo "FAULT NOT ACTIVE: no AICpu stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
