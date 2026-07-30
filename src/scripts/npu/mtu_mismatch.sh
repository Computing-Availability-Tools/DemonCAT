#!/bin/sh
# rNPU_mtu_mismatch: RoCE MTU change. Clean = restore original MTU from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
size=${DCAT_PARAM_SIZE:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -mtu -g 2>/dev/null | grep -oE 'mtu [0-9]+' | grep -oE '[0-9]+')
    orig=$(sidecar_load rNPU_mtu_mismatch "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${size:?missing required param: size}
        npu_check_env
        orig=$($HCCN -mtu -g 2>/dev/null | grep -oE 'mtu [0-9]+' | grep -oE '[0-9]+')
        [ -n "$orig" ] && sidecar_save rNPU_mtu_mismatch "$chip" "$orig"
        $HCCN -mtu -s size "$size" || { echo "mtu set failed" >&2; exit 1; }
        echo "applied mtu $size on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_mtu_mismatch-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_mtu_mismatch-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored mtu (all chips)" || echo "restored mtu (no active injection)"
        elif fault_present; then
            orig=$(sidecar_load rNPU_mtu_mismatch "$chip"); orig=${orig:-1500}
            $HCCN -mtu -s size "$orig" || { echo "mtu restore failed" >&2; exit 1; }
            sidecar_clear rNPU_mtu_mismatch "$chip"
            echo "restored mtu to $orig on chip $chip"
        else echo "mtu already at original, no-op"; fi
        ;;
    query) $HCCN -mtu -g; fault_present ;;
esac
