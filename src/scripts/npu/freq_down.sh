#!/bin/sh
# rNPU_freq_down: query AICore frequency via pydcmi.
# 910B4 does not support SETTING frequency; this fault QUERIES and reports it.
# inject: query current/max AICore freq, record in sidecar, report if throttled
# clean:  clear sidecar
# query:  npu-smi info -t usages
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_freq_down-$chip.bak"

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        freq_info=$(python3 -c "
import pydcmi.dcmi_api_v2 as d
d.dcmi_init()
cur = d.dcmi_get_device_frequency($chip, 0, d.DcmiFreqType.DCMI_FREQ_AICORE_CURRENT_)
mx  = d.dcmi_get_device_frequency($chip, 0, d.DcmiFreqType.DCMI_FREQ_AICORE_MAX)
print(f'{cur} {mx}')
" 2>/dev/null)
        if [ -z "$freq_info" ]; then
            echo "failed to query AICore frequency on chip $chip" >&2
            exit 1
        fi
        cur_freq=$(echo "$freq_info" | awk '{print $1}')
        max_freq=$(echo "$freq_info" | awk '{print $2}')
        echo "${cur_freq}/${max_freq}" > "$SIDECAR"
        echo "AICore freq on chip $chip: ${cur_freq}MHz (max ${max_freq}MHz)"
        if [ "$cur_freq" -lt "$max_freq" ] 2>/dev/null; then
            echo "WARNING: AICore running below max frequency (${cur_freq}/${max_freq}MHz)"
        fi
        ;;
    clean)
        rm -f "$SIDECAR" 2>/dev/null
        echo "frequency monitor cleared on chip $chip"
        ;;
    query)
        if [ -f "$SIDECAR" ]; then
            echo "FAULT CONFIRMED: freq monitor active"
            cat "$SIDECAR"
            exit 0
        else
            echo "FAULT NOT ACTIVE: no freq monitor"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
