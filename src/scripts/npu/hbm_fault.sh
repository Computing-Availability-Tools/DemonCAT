#!/bin/sh
# rNPU_hbm_fault: occupy HBM memory via torch_npu tensor allocation.
# inject: run python stress script (background, duration-limited)
# clean:  kill the stress process (frees HBM)
# query:  npu-smi info -t usages (check HBM Usage Rate%)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
size_gb=${DCAT_PARAM_SIZE_GB:-5}
duration=${DCAT_PARAM_DURATION:-60}
npu_validate_chip "$chip"
SIDECAR="/tmp/dcat-rNPU_hbm_fault-$chip.pid"

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        script_dir=$(cd "$(dirname "$0")" && pwd)
        python3 "$script_dir/_npu_stress.py" --chip "$chip" --task hbm --hbm_gb "$size_gb" --duration "$duration" &
        pid=$!
        printf '%s\n' "$pid" > "$SIDECAR"
        sleep 2
        kill -0 "$pid" 2>/dev/null || { echo "hbm stress failed to start (needs torch_npu + pydcmi)" >&2; rm -f "$SIDECAR"; exit 1; }
        echo "injected hbm stress on chip $chip (pid=$pid, size=${size_gb}GB, duration=${duration}s)"
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            pid=$(cat "$SIDECAR")
            kill "$pid" 2>/dev/null; kill -9 "$pid" 2>/dev/null
            rm -f "$SIDECAR"
            echo "cleaned hbm stress on chip $chip (killed pid=$pid)"
        else echo "no active hbm stress"; fi
        ;;
    query)
        npu-smi info -t usages -i "$chip" -c 0 2>/dev/null | grep -i 'HBM'
        if [ -f "$SIDECAR" ]; then exit 0; else exit 1; fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
