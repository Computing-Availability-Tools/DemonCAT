#!/bin/sh
# rNPU_ip_change: RoCE IP change. Clean = restore original IP+netmask from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
addr=${DCAT_PARAM_ADDRESS:-}
mask=${DCAT_PARAM_NETMASK:-}
HCCN="hccn_tool -i $chip"
SIDECAR_FN() { echo "/tmp/dcat-rNPU_ip_change-$chip.bak"; }

fault_present() {
    cur=$($HCCN -ip -g 2>/dev/null)
    orig_addr=
    _sc=$(SIDECAR_FN)
    [ -f "$_sc" ] && orig_addr=$(grep '^address=' "$_sc" | cut -d= -f2)
    cur_addr=$(echo "$cur" | grep -oE 'ipaddr:[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+$')
    [ -n "$orig_addr" ] && [ "$cur_addr" != "$orig_addr" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${addr:?missing required param: address}
        : ${mask:?missing required param: netmask}
        npu_check_env
        cur=$($HCCN -ip -g 2>/dev/null)
        o_addr=$(echo "$cur" | grep -oE 'ipaddr:[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+')
        o_mask=$(echo "$cur" | grep -oE 'netmask:[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+')
        _sc=$(SIDECAR_FN)
        [ -n "$o_addr" ] && printf 'address=%s\nnetmask=%s\n' "$o_addr" "$o_mask" > "$_sc"
        $HCCN -ip -s address "$addr" netmask "$mask" || { echo "ip set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_ip_change 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied ip $addr/$mask on chip $chip (was $o_addr/$o_mask)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_ip_change-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_ip_change-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored ip (all chips)" || echo "restored ip (no active injection)"
        elif fault_present; then
            _sc=$(SIDECAR_FN)
            address=$(grep '^address=' "$_sc" 2>/dev/null | cut -d= -f2)
            netmask=$(grep '^netmask=' "$_sc" 2>/dev/null | cut -d= -f2)
            : ${address:=0.0.0.0}; : ${netmask:=255.255.255.0}
            $HCCN -ip -s address "$address" netmask "$netmask" || { echo "ip restore failed" >&2; exit 1; }
            rm -f "$_sc"
            echo "restored ip to $address/$netmask on chip $chip"
        else echo "ip already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -ip -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
