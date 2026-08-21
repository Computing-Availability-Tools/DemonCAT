#!/bin/sh
# rNPU_iprule: ip rule poisoning (inject=add, clean=del).
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
dir=${DCAT_PARAM_DIR:-}
ip=${DCAT_PARAM_IP:-}
table=${DCAT_PARAM_TABLE:-}
HCCN="$HCCN_TO hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_iprule-$chip.bak"

fault_present() {
    case "${DCAT_OP:-inject}" in
        clean) ! $HCCN -ip_rule -g 2>/dev/null | grep -Fq "$ip" ;;
        *)     $HCCN -ip_rule -g 2>/dev/null | grep -F "$ip" | grep -Fq "$table" ;;
    esac
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${dir:?missing required param: dir}
        : ${ip:?missing required param: ip}
        : ${table:?missing required param: table}
        npu_check_env
        $HCCN -ip_rule -a dir "$dir" ip "$ip" table "$table" || { echo "ip_rule add failed" >&2; exit 1; }
        printf 'dir=%s\nip=%s\n' "$dir" "$ip" > "$SIDECAR"
        fault_present || { echo "rNPU_iprule 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "added ip_rule $dir $ip -> table $table on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0; failed=0
            for bak in /tmp/dcat-rNPU_iprule-*.bak; do
                [ -f "$bak" ] || continue
                cleaned=1
                c=${bak##*/dcat-rNPU_iprule-}; c=${c%.bak}
                d=$(grep '^dir=' "$bak" 2>/dev/null | cut -d= -f2-)
                i=$(grep '^ip=' "$bak" 2>/dev/null | cut -d= -f2-)
                if DCAT_OP=clean DCAT_PARAM_CHIP="$c" DCAT_PARAM_DIR="$d" DCAT_PARAM_IP="$i" "$0" >/dev/null 2>&1; then :
                else echo "clean failed for chip $c" >&2; failed=1; fi
            done
            [ "$failed" = 1 ] && { echo "ip_rule: some cleans failed (state preserved)" >&2; exit 1; }
            [ "$cleaned" = 1 ] && echo "cleaned ip_rule (all chips)" || echo "cleaned ip_rule (no active injection)"
        else
            : ${dir:?missing required param: dir}
            : ${ip:?missing required param: ip}
            $HCCN -ip_rule -d dir "$dir" ip "$ip" || { echo "ip_rule del failed" >&2; exit 1; }
            fault_present || { echo "rNPU_iprule 清除回读校验失败:动作未生效" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "removed ip_rule $dir $ip on chip $chip"
        fi
        ;;
    query) npu_foreach_chip '$HCCN -ip_rule -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
