#!/bin/sh
# rNPU_netdetect_change: netdetect IP change. Clean = restore original from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
addr=${DCAT_PARAM_ADDRESS:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -netdetect -g 2>/dev/null | grep -oE 'address:[[:space:]]*[0-9.]+' | awk '{print $NF}')
    orig=$(sidecar_load rNPU_netdetect_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${addr:?missing required param: address}
        npu_check_env
        orig=$($HCCN -netdetect -g 2>/dev/null | grep -oE 'address:[[:space:]]*[0-9.]+' | awk '{print $NF}')
        [ -n "$orig" ] && sidecar_save rNPU_netdetect_change "$chip" "$orig"
        $HCCN -netdetect -s address "$addr" || { echo "netdetect set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_netdetect_change 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied netdetect $addr on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            failed=0
            for bak in /tmp/dcat-rNPU_netdetect_change-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_netdetect_change-}; c=${c%.bak}
                if DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1; then
                    cleaned=1
                else
                    echo "restore failed for chip $c" >&2
                    failed=1
                fi
            done
            if [ "$failed" = 1 ]; then
                echo "netdetect_change: some restores failed (state preserved)" >&2; exit 1
            elif [ "$cleaned" = 1 ]; then
                echo "restored netdetect (all chips)"
            else
                echo "restored netdetect (no active injection)"
            fi
        elif fault_present; then
            orig=$(sidecar_load rNPU_netdetect_change "$chip"); orig=${orig:-0.0.0.0}
            $HCCN -netdetect -s address "$orig" || { echo "netdetect restore failed" >&2; exit 1; }
            sidecar_clear rNPU_netdetect_change "$chip"
            echo "restored netdetect to $orig on chip $chip"
        else echo "netdetect already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -netdetect -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
