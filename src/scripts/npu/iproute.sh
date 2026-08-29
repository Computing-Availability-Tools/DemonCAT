#!/bin/sh
# rNPU_iproute: ip route poisoning (inject=add, clean=del).
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
ip=${DCAT_PARAM_IP:-}
mask=${DCAT_PARAM_IP_MASK:-}
via=${DCAT_PARAM_VIA:-}
dev=${DCAT_PARAM_DEV:-}
table=${DCAT_PARAM_TABLE:-}
HCCN="$HCCN_TO hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_iproute-$chip.bak"

# 0.0.0.0/0（默认路由）hccn 渲染为 "default"，grep 明文 ip 会回读失败 → 特殊符号等价匹配
_match_ctx() {
    if [ "$ip" = "0.0.0.0" ] && [ "$mask" = "0" ]; then grep -Fq "default"; else grep -Fq "$ip"; fi
}

fault_present() {
    case "${DCAT_OP:-inject}" in
        clean) ! $HCCN -ip_route -g table "$table" 2>/dev/null | _match_ctx ;;
        *)     $HCCN -ip_route -g table "$table" 2>/dev/null | _match_ctx ;;
    esac
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${ip:?missing required param: ip}
        : ${mask:?missing required param: ip_mask}
        : ${table:?missing required param: table}
        : ${via:?missing required param: via}
        : ${dev:?missing required param: dev}
        npu_check_env
        $HCCN -ip_route -a ip "$ip" ip_mask "$mask" via "$via" dev "$dev" table "$table" || { echo "ip_route add failed" >&2; exit 1; }
        printf 'ip=%s\nmask=%s\ntable=%s\n' "$ip" "$mask" "$table" > "$SIDECAR"
        fault_present || { echo "rNPU_iproute 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "added ip_route $ip/$mask via $via dev $dev table $table on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0; failed=0
            for bak in /tmp/dcat-rNPU_iproute-*.bak; do
                [ -f "$bak" ] || continue
                cleaned=1
                c=${bak##*/dcat-rNPU_iproute-}; c=${c%.bak}
                i=$(grep '^ip=' "$bak" 2>/dev/null | cut -d= -f2-)
                m=$(grep '^mask=' "$bak" 2>/dev/null | cut -d= -f2-)
                t=$(grep '^table=' "$bak" 2>/dev/null | cut -d= -f2-)
                if DCAT_OP=clean DCAT_PARAM_CHIP="$c" DCAT_PARAM_IP="$i" DCAT_PARAM_IP_MASK="$m" DCAT_PARAM_TABLE="$t" "$0" >/dev/null 2>&1; then :
                else echo "clean failed for chip $c" >&2; failed=1; fi
            done
            [ "$failed" = 1 ] && { echo "ip_route: some cleans failed (state preserved)" >&2; exit 1; }
            [ "$cleaned" = 1 ] && echo "cleaned ip_route (all chips)" || echo "cleaned ip_route (no active injection)"
        else
            : ${ip:?missing required param: ip}
            : ${mask:?missing required param: ip_mask}
            : ${table:?missing required param: table}
            $HCCN -ip_route -d ip "$ip" ip_mask "$mask" table "$table" || { echo "ip_route del failed" >&2; exit 1; }
            fault_present || { echo "rNPU_iproute 清除回读校验失败:动作未生效" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "removed ip_route $ip/$mask table $table on chip $chip"
        fi
        ;;
    query)
        if [ -n "$ip$table" ]; then
            npu_foreach_chip '[ -n "$table" ] && $HCCN -ip_route -g table "$table"; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }'
        else
            npu_query_noargs rNPU_iproute '$HCCN -ip_route -g table "$table" 2>/dev/null | grep -Fq "$ip"'
        fi
        ;;
esac
