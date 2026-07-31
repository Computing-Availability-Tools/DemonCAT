#!/bin/sh
# rNPU_iprule_del: delete ip rule. Clean = re-add with original table from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
dir=${DCAT_PARAM_DIR:-}
ip=${DCAT_PARAM_IP:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    if [ -n "$ip" ]; then ! $HCCN -ip_rule -g 2>/dev/null | grep -Fq "$ip"
    else [ -f "/tmp/dcat-rNPU_iprule_del-$chip.bak" ]; fi
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${dir:?missing required param: dir}
        : ${ip:?missing required param: ip}
        npu_check_env
        orig_table=$($HCCN -ip_rule -g 2>/dev/null | grep "$ip" | grep -oE 'lookup [0-9]+' | awk '{print $2}')
        [ -n "$orig_table" ] && sidecar_save rNPU_iprule_del "$chip" "$orig_table"
        $HCCN -ip_rule -d dir "$dir" ip "$ip" || { echo "ip_rule del failed" >&2; exit 1; }
        echo "deleted ip_rule $dir $ip on chip $chip (was table $orig_table)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for ip_rule clean)"
        elif fault_present; then
            orig_table=$(sidecar_load rNPU_iprule_del "$chip"); orig_table=${orig_table:-0}
            $HCCN -ip_rule -a dir "$dir" ip "$ip" table "$orig_table" || { echo "ip_rule re-add failed" >&2; exit 1; }
            sidecar_clear rNPU_iprule_del "$chip"
            echo "restored ip_rule $dir $ip -> table $orig_table on chip $chip"
        else echo "ip_rule already present, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -ip_rule -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
