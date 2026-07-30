#!/bin/sh
# rNPU_iprule_add: add ip rule. Clean = delete the added rule.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
dir=${DCAT_PARAM_DIR:-}
ip=${DCAT_PARAM_IP:-}
table=${DCAT_PARAM_TABLE:-}
HCCN="hccn_tool -i $chip"

fault_present() { [ -n "$ip" ] && [ -n "$table" ] && $HCCN -ip_rule -g 2>/dev/null | grep -F "$ip" | grep -Fq "$table"; }

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        $HCCN -ip_rule -a dir "$dir" ip "$ip" table "$table" || { echo "ip_rule add failed" >&2; exit 1; }
        echo "added ip_rule $dir $ip -> table $table on chip $chip"
        ;;
    clean)
        if fault_present; then
            $HCCN -ip_rule -d dir "$dir" ip "$ip" || { echo "ip_rule del failed" >&2; exit 1; }
            echo "removed ip_rule $dir $ip on chip $chip"
        else echo "ip_rule not present, no-op"; fi
        ;;
    query) $HCCN -ip_rule -g; fault_present ;;
esac
