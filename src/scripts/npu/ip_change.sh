#!/bin/sh
# rNPU_ip_change: RoCE IP change. Clean = restore original IP+netmask from sidecar.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
addr=${DCAT_PARAM_ADDRESS:?missing required param: address}
mask=${DCAT_PARAM_NETMASK:?missing required param: netmask}
HCCN="hccn_tool -i $chip"
SIDECAR="/tmp/dcat-rNPU_ip_change-$chip.bak"

fault_present() {
    cur=$($HCCN -ip -g 2>/dev/null)
    [ -f "$SIDECAR" ] && orig_addr=$(grep '^address=' "$SIDECAR" | cut -d= -f2)
    [ -n "$orig_addr" ] && ! echo "$cur" | grep -Fq "$orig_addr"
}

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        cur=$($HCCN -ip -g 2>/dev/null)
        o_addr=$(echo "$cur" | grep -oE 'address [0-9.]+' | awk '{print $2}')
        o_mask=$(echo "$cur" | grep -oE 'netmask [0-9.]+' | awk '{print $2}')
        [ -n "$o_addr" ] && printf 'address=%s\nnetmask=%s\n' "$o_addr" "$o_mask" > "$SIDECAR"
        $HCCN -ip -s address "$addr" netmask "$mask" || { echo "ip set failed" >&2; exit 1; }
        fault_present || { echo "rNPU_ip_change 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "applied ip $addr/$mask on chip $chip (was $o_addr/$o_mask)"
        ;;
    clean)
        if fault_present; then
            . "$SIDECAR"
            : ${address:=0.0.0.0}; : ${netmask:=255.255.255.0}
            $HCCN -ip -s address "$address" netmask "$netmask" || { echo "ip restore failed" >&2; exit 1; }
            rm -f "$SIDECAR"
            echo "restored ip to $address/$netmask on chip $chip"
        else echo "ip already at original, no-op"; fi
        ;;
    query) $HCCN -ip -g; fault_present ;;
esac
