#!/bin/sh
# rNPU_mtu_mismatch: RoCE MTU change. Clean = restore original MTU from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
size=${DCAT_PARAM_SIZE:?missing required param: size}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -mtu -g 2>/dev/null | grep -oE 'mtu:[[:space:]]*[0-9]+' | grep -oE '[0-9]+')
    orig=$(sidecar_load rNPU_mtu_mismatch "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        orig=$($HCCN -mtu -g 2>/dev/null | grep -oE 'mtu:[[:space:]]*[0-9]+' | grep -oE '[0-9]+')
        [ -n "$orig" ] && sidecar_save rNPU_mtu_mismatch "$chip" "$orig"
        $HCCN -mtu -s size "$size" || { echo "mtu set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_mtu_mismatch 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied mtu $size on chip $chip (was $orig)"
        ;;
    clean)
        if fault_present; then
            orig=$(sidecar_load rNPU_mtu_mismatch "$chip"); orig=${orig:-1500}
            $HCCN -mtu -s size "$orig" || { echo "mtu restore failed" >&2; exit 1; }
            sidecar_clear rNPU_mtu_mismatch "$chip"
            echo "restored mtu to $orig on chip $chip"
        else echo "mtu already at original, no-op"; fi
        ;;
    query) $HCCN -mtu -g; fault_present ;;
esac
