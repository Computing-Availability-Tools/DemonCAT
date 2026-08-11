#!/bin/sh
# rNPU_aic_fault: stress AI-core to 100% via torch_npu matmul loop.
# inject: run python stress script (background, duration-limited)
# clean:  kill the stress process
# query:  npu-smi info -t usages (check AICore%)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
duration=${DCAT_PARAM_DURATION:-60}
npu_validate_chip "$chip"
SIDECAR="/tmp/dcat-rNPU_aic_fault-$chip.pid"

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        script_dir=$(cd "$(dirname "$0")" && pwd)
        python3 "$script_dir/_npu_stress.py" --chip "$chip" --task aicore --duration "$duration" &
        pid=$!
        printf '%s\n' "$pid" > "$SIDECAR"
        sleep 2
        kill -0 "$pid" 2>/dev/null || { echo "aic stress failed to start (needs torch_npu + pydcmi)" >&2; rm -f "$SIDECAR"; exit 1; }
        echo "injected aicore stress on chip $chip (pid=$pid, duration=${duration}s)"
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            pid=$(cat "$SIDECAR")
            kill "$pid" 2>/dev/null; kill -9 "$pid" 2>/dev/null
            rm -f "$SIDECAR"
            echo "cleaned aicore stress on chip $chip (killed pid=$pid)"
        else echo "no active aicore stress"; fi
        ;;
    query)
        npu-smi info -t usages -i "$chip" -c 0 2>/dev/null | grep -i 'Aicore'
        if [ -f "$SIDECAR" ]; then exit 0; else exit 1; fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
