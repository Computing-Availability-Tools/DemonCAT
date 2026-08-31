#!/bin/sh
# rNPU_mtu_mismatch: RoCE MTU change. Clean = restore original MTU from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
size=${DCAT_PARAM_SIZE:-}
HCCN="hccn"

fault_present() {
    cur=$($HCCN -mtu -g 2>/dev/null | grep -oE 'mtu:[[:space:]]*[0-9]+' | grep -oE '[0-9]+')
    orig=$(sidecar_load rNPU_mtu_mismatch "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${size:?missing required param: size}
        npu_check_env
        orig=$($HCCN -mtu -g 2>/dev/null | grep -oE 'mtu:[[:space:]]*[0-9]+' | grep -oE '[0-9]+')
        [ -n "$orig" ] && sidecar_save rNPU_mtu_mismatch "$chip" "$orig"
        $HCCN -mtu -s size "$size" || { echo "mtu set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_mtu_mismatch 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied mtu $size on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            failed=0
            for bak in /tmp/dcat-rNPU_mtu_mismatch-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_mtu_mismatch-}; c=${c%.bak}
                if DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1; then
                    cleaned=1
                else
                    echo "restore failed for chip $c" >&2
                    failed=1
                fi
            done
            if [ "$failed" = 1 ]; then
                echo "mtu_mismatch: some restores failed (state preserved)" >&2; exit 1
            elif [ "$cleaned" = 1 ]; then
                echo "restored mtu (all chips)"
            else
                echo "restored mtu (no active injection)"
            fi
        elif fault_present; then
            orig=$(sidecar_load rNPU_mtu_mismatch "$chip"); orig=${orig:-1500}
            $HCCN -mtu -s size "$orig" || { echo "mtu restore failed" >&2; exit 1; }
            sidecar_clear rNPU_mtu_mismatch "$chip"
            echo "restored mtu to $orig on chip $chip"
        else echo "mtu already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -mtu -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
