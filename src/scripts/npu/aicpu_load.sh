#!/bin/sh
# rNPU_aicpu_load: AICpu stress via aclnnTopk FP64.
# inject: run _npu_stress aicpu in background, write pidfile
# clean:  kill stress process(es)
# query:  npu-smi info -t usages (check Aicpu Usage Rate)
# 
# Adaptive: 910B single topk fills AICPU (~94%); 910C needs 6 parallel.
# Probe with 1 process at 100%, check npu-smi; if <50% → 910C mode (6 procs).
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

        # Phase 1: probe with 1 process at 100% to detect hardware type
        LOG="/tmp/dcat-rNPU_aicpu_load-$chip.log"
        "$STRESS_BIN" aicpu "$dev_id" 0 100 0 > "$LOG" 2>&1 &
        probe_pid=$!
        echo "$probe_pid" > "$SIDECAR"
        sleep 5
        if ! kill -0 "$probe_pid" 2>/dev/null; then
            rm -f "$SIDECAR"
            echo "AICpu stress failed on chip $chip:" >&2
            tail -3 "$LOG" >&2
            rm -f "$LOG"
            exit 1
        fi
        rm -f "$LOG"

        # Phase 2: check AICPU utilization to determine mode
        card_chip=$(npu_phy_to_card "$chip"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
        probe_pct=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null | awk '/Aicpu/{print $NF}')
        probe_pct=${probe_pct:-0}

        if [ "$probe_pct" -ge 50 ]; then
            # 910B: single process fills AICPU. Keep probe, adjust load_pct if needed.
            if [ "$load_pct" != "100" ]; then
                kill -9 "$probe_pid" 2>/dev/null
                wait "$probe_pid" 2>/dev/null
                rm -f "$SIDECAR"
                "$STRESS_BIN" aicpu "$dev_id" 0 "$load_pct" 0 > /dev/null 2>&1 &
                echo $! > "$SIDECAR"
                sleep 3
            fi
            echo "AICpu stress started on chip $chip (dev $dev_id, pid $(cat "$SIDECAR"), load=${load_pct}%, 1 proc)"
        else
            # 910C: need parallel processes. Kill probe, relaunch with N procs.
            kill -9 "$probe_pid" 2>/dev/null
            wait "$probe_pid" 2>/dev/null
            rm -f "$SIDECAR"

            if [ "$load_pct" -ge 100 ]; then
                nprocs=6
            else
                # Process-count scaling: ceil(load_pct/100 * 6)
                nprocs=$(( (load_pct * 6 + 99) / 100 ))
                [ "$nprocs" -lt 1 ] && nprocs=1
            fi

            pids=""
            i=0
            while [ "$i" -lt "$nprocs" ]; do
                "$STRESS_BIN" aicpu "$dev_id" 0 100 0 > /dev/null 2>&1 &
                pids="$pids $!"
                i=$((i + 1))
            done
            echo "$pids" > "$SIDECAR"
            sleep 5

            # Verify at least one process alive
            alive=0
            for p in $pids; do kill -0 "$p" 2>/dev/null && alive=$((alive+1)); done
            if [ "$alive" -eq 0 ]; then
                rm -f "$SIDECAR"
                echo "AICpu stress failed on chip $chip (all $nprocs procs died)" >&2
                exit 1
            fi
            echo "AICpu stress started on chip $chip (dev $dev_id, procs=$nprocs alive=$alive, load~${load_pct}%)"
        fi
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            for pid in $(cat "$SIDECAR"); do
                kill -9 "$pid" 2>/dev/null
            done
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
                alive=0
                for pid in $(cat "$f" 2>/dev/null); do
                    kill -0 "$pid" 2>/dev/null && alive=$((alive+1))
                done
                [ "$alive" -eq 0 ] && { rm -f "$f"; continue; }
                echo "FAULT CONFIRMED: AICpu stress active on chip $c ($alive procs)"
                card_chip=$(npu_phy_to_card "$c"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
                aicpu_pct=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null | awk '/Aicpu/{print $NF}')
                echo "  AICpu Usage(%): ${aicpu_pct:-?}"
                found=1
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE: no AICpu stress"; exit 1; }
        elif [ -f "$SIDECAR" ]; then
            alive=0
            for pid in $(cat "$SIDECAR" 2>/dev/null); do
                kill -0 "$pid" 2>/dev/null && alive=$((alive+1))
            done
            if [ "$alive" -gt 0 ]; then
                echo "FAULT CONFIRMED: AICpu stress active ($alive procs)"
                card_chip=$(npu_phy_to_card "$chip"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
                aicpu_pct=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null | awk '/Aicpu/{print $NF}')
                echo "AICpu Usage(%): ${aicpu_pct:-?}"
                exit 0
            else
                rm -f "$SIDECAR" 2>/dev/null
                echo "FAULT NOT ACTIVE: no AICpu stress"
                exit 1
            fi
        else
            echo "FAULT NOT ACTIVE: no AICpu stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
