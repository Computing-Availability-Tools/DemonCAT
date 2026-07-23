#!/bin/sh
# rNPU_netdetect_change: netdetect IP change. Clean = restore original from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
addr=${DCAT_PARAM_ADDRESS:?missing required param: address}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -netdetect -g 2>/dev/null | grep -oE 'address [0-9.]+' | awk '{print $2}')
    orig=$(sidecar_load rNPU_netdetect_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        orig=$($HCCN -netdetect -g 2>/dev/null | grep -oE 'address [0-9.]+' | awk '{print $2}')
        [ -n "$orig" ] && sidecar_save rNPU_netdetect_change "$chip" "$orig"
        $HCCN -netdetect -s address "$addr" || { echo "netdetect set failed" >&2; exit 1; }
        echo "applied netdetect $addr on chip $chip (was $orig)"
        ;;
    clean)
        if fault_present; then
            orig=$(sidecar_load rNPU_netdetect_change "$chip"); orig=${orig:-0.0.0.0}
            $HCCN -netdetect -s address "$orig" || { echo "netdetect restore failed" >&2; exit 1; }
            sidecar_clear rNPU_netdetect_change "$chip"
            echo "restored netdetect to $orig on chip $chip"
        else echo "netdetect already at original, no-op"; fi
        ;;
    query) $HCCN -netdetect -g; fault_present ;;
esac
