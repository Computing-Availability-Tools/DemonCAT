#!/bin/sh
# rNPU_freq_down: lower NPU frequency.
# inject: npu-smi set -t reset (chip reset → frequency drop on restart)
#         fallback: ipmitool dcmi power-cap (if BMC supports DCMI)
#         fallback: hccn_tool -t freq -s (if subcommand exists)
# clean:  hccn_tool -cfg recovery (restore config)
# query:  npu-smi info -t power + pydcmi frequency
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
freq=${DCAT_PARAM_FREQ:-}
npu_validate_chip "$chip"
SIDECAR="/tmp/dcat-rNPU_freq_down-$chip.bak"

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env 2>/dev/null || true
        # Path 1: hccn_tool freq set (if supported)
        if [ -n "$freq" ]; then
            orig=$(hccn_tool -i "$chip" -t freq -g 2>/dev/null | grep -oE '[0-9]+' | head -1)
            if [ -n "$orig" ] && hccn_tool -i "$chip" -t freq -s "$freq" 2>/dev/null; then
                printf '%s\n' "$orig" > "$SIDECAR"
                echo "set npu freq=$freq on chip $chip via hccn_tool (orig=$orig)"
                exit 0
            fi
        fi
        # Path 2: ipmitool DCMI power-cap (if BMC supports)
        if command -v ipmitool >/dev/null 2>&1 && [ -n "$freq" ]; then
            bmc_ip=${DCAT_PARAM_BMC_IP:-${BMC_IP:-}}
            bmc_u=${DCAT_PARAM_BMC_USER:-${BMC_USER:-}}
            bmc_p=${DCAT_PARAM_BMC_PASS:-${BMC_PASS:-}}
            if [ -n "$bmc_ip" ]; then
                if ipmitool -I lanplus -H "$bmc_ip" -U "$bmc_u" -P "$bmc_p" dcmi power set_limit limit "$freq" 2>/dev/null; then
                    printf 'ipmitool:%s\n' "$freq" > "$SIDECAR"
                    echo "set npu power-cap=$freq via ipmitool (BMC-level)"
                    exit 0
                fi
            fi
        fi
        # Path 3: npu-smi frequency set (if supported by hardware)
        if [ -n "$freq" ] && npu-smi set -t freq -i "$chip" -c 0 -d "$freq" 2>/dev/null; then
            printf 'npu-smi:%s\n' "$freq" > "$SIDECAR"
            echo "set npu freq=$freq via npu-smi"
            exit 0
        fi
        echo "freq_down: no supported freq tool on this hardware" >&2
        echo "available: hccn_tool -t freq / ipmitool dcmi power-cap / npu-smi set -t freq" >&2
        echo "this hardware (910B4) may not support frequency control" >&2
        exit 1
        ;;
    clean)
        if [ -f "$SIDECAR" ]; then
            method=$(head -1 "$SIDECAR")
            case "$method" in
                ipmitool:*)
                    freq_val=${method#ipmitool:}
                    bmc_ip=${DCAT_PARAM_BMC_IP:-${BMC_IP:-}}
                    bmc_u=${DCAT_PARAM_BMC_USER:-${BMC_USER:-}}
                    bmc_p=${DCAT_PARAM_BMC_PASS:-${BMC_PASS:-}}
                    [ -n "$bmc_ip" ] && ipmitool -I lanplus -H "$bmc_ip" -U "$bmc_u" -P "$bmc_p" dcmi power deactivate 2>/dev/null
                    ;;
                npu-smi:*)
                    orig_val=${method#npu-smi:}
                    npu-smi set -t freq -i "$chip" -c 0 -d "$orig_val" 2>/dev/null || true
                    ;;
                *)
                    orig_freq="$method"
                    hccn_tool -i "$chip" -t freq -s "$orig_freq" 2>/dev/null || true
                    ;;
            esac
            rm -f "$SIDECAR"
            echo "restored npu freq on chip $chip"
        else
            echo "no active freq_down" >&2; exit 1
        fi
        ;;
    query)
        npu-smi info -t power -i "$chip" -c 0 2>/dev/null
        python3 -c "
import pydcmi.dcmi_api_v2 as d
d.dcmi_init()
r = d.dcmi_get_device_frequency($chip, 0, 0)
print(f'AICore Freq: {r}')
" 2>/dev/null || true
        [ -f "$SIDECAR" ] && exit 0 || exit 1
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
