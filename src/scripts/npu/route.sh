#!/bin/sh
# rNPU_route: RoCE route poisoning (inject=add, clean=del).
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
addr=${DCAT_PARAM_ADDRESS:-}
mask=${DCAT_PARAM_NETMASK:-}
gw=${DCAT_PARAM_GATEWAY:-}
HCCN="$HCCN_TO hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_route-$chip.bak"

fault_present() {
    case "${DCAT_OP:-inject}" in
        clean) ! $HCCN -route -g 2>/dev/null | grep -Fq "$addr" ;;
        *)     $HCCN -route -g 2>/dev/null | grep -Fq "$addr" ;;
    esac
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${addr:?missing required param: address}
        : ${mask:?missing required param: netmask}
        : ${gw:?missing required param: gateway}
        npu_check_env
        $HCCN -route -a address "$addr" netmask "$mask" gateway "$gw" || { echo "route add failed" >&2; exit 1; }
        printf 'addr=%s\nmask=%s\n' "$addr" "$mask" > "$SIDECAR"
        fault_present || { echo "rNPU_route 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "added route $addr/$mask via $gw on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0; failed=0
            for bak in /tmp/dcat-rNPU_route-*.bak; do
                [ -f "$bak" ] || continue
                cleaned=1
                c=${bak##*/dcat-rNPU_route-}; c=${c%.bak}
                a=$(grep '^addr=' "$bak" 2>/dev/null | cut -d= -f2-)
                m=$(grep '^mask=' "$bak" 2>/dev/null | cut -d= -f2-)
                if DCAT_OP=clean DCAT_PARAM_CHIP="$c" DCAT_PARAM_ADDRESS="$a" DCAT_PARAM_NETMASK="$m" "$0" >/dev/null 2>&1; then :
                else echo "clean failed for chip $c" >&2; failed=1; fi
            done
            [ "$failed" = 1 ] && { echo "route: some cleans failed (state preserved)" >&2; exit 1; }
            [ "$cleaned" = 1 ] && echo "cleaned route (all chips)" || echo "cleaned route (no active injection)"
        else
            : ${addr:?missing required param: address}
            : ${mask:?missing required param: netmask}
            $HCCN -route -d address "$addr" netmask "$mask" || { echo "route del failed" >&2; exit 1; }
            fault_present || { echo "rNPU_route 清除回读校验失败:动作未生效" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "removed route $addr/$mask on chip $chip"
        fi
        ;;
    query) npu_foreach_chip '$HCCN -route -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
