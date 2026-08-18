#!/bin/sh
# rNPU_iprule: ip rule manipulation (add/delete). Clean auto-undoes.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
action=${DCAT_PARAM_ACTION:-}
dir=${DCAT_PARAM_DIR:-}
ip=${DCAT_PARAM_IP:-}
table=${DCAT_PARAM_TABLE:-}
HCCN="hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_iprule-$chip.bak"

is_del_action() { [ -f "$SIDECAR" ]; }

fault_present() {
    if is_del_action; then
        ! $HCCN -ip_rule -g 2>/dev/null | grep -Fq "$ip"
    else
        $HCCN -ip_rule -g 2>/dev/null | grep -F "$ip" | grep -Fq "$table"
    fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${action:?missing required param: action (add or del)}
        : ${dir:?missing required param: dir}
        : ${ip:?missing required param: ip}
        npu_check_env
        case "$action" in
            add)
                : ${table:?missing required param: table (required for action=add)}
                $HCCN -ip_rule -a dir "$dir" ip "$ip" table "$table" || { echo "ip_rule add failed" >&2; exit 1; }
                fault_present || { echo "rNPU_iprule 注入回读校验失败:动作未生效" >&2; exit 1; }
                echo "added ip_rule $dir $ip -> table $table on chip $chip"
                ;;
            del)
                orig_table=$($HCCN -ip_rule -g 2>/dev/null | grep "$ip" | grep -oE 'lookup [0-9]+' | awk '{print $2}')
                printf '%s\n' "${orig_table:-}" > "$SIDECAR"
                $HCCN -ip_rule -d dir "$dir" ip "$ip" || { echo "ip_rule del failed" >&2; exit 1; }
                fault_present || { echo "rNPU_iprule 注入回读校验失败:动作未生效" >&2; exit 1; }
                echo "deleted ip_rule $dir $ip on chip $chip (was table ${orig_table:-none})"
                ;;
            *) echo "invalid action: $action (expected add or del)" >&2; exit 1 ;;
        esac
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_iprule-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_iprule-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored ip_rule (all chips)" || echo "restored ip_rule (no active injection)"
        elif is_del_action; then
            orig_table=$(cat "$SIDECAR" 2>/dev/null); orig_table=${orig_table:-0}
            $HCCN -ip_rule -a dir "$dir" ip "$ip" table "$orig_table" || { echo "ip_rule re-add failed" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "restored ip_rule $dir $ip -> table $orig_table on chip $chip"
        elif fault_present; then
            $HCCN -ip_rule -d dir "$dir" ip "$ip" || { echo "ip_rule del failed" >&2; exit 1; }
            echo "removed ip_rule $dir $ip on chip $chip"
        else echo "ip_rule not present, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -ip_rule -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
