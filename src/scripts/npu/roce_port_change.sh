#!/bin/sh
# rNPU_roce_port_change: RoCE UDP port change. Clean = restore original port (default 4791).
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
npu_validate_chip "$chip"
port=${DCAT_PARAM_PORT:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -udp -g 2>/dev/null | grep -oE 'udp_port:[0-9]+' | grep -oE '[0-9]+')
    orig=$(sidecar_load rNPU_roce_port_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        orig=$($HCCN -udp -g 2>/dev/null | grep -oE 'udp_port:[0-9]+' | grep -oE '[0-9]+')
        [ -n "$orig" ] && sidecar_save rNPU_roce_port_change "$chip" "$orig"
        $HCCN -udp -s port "$port" || { echo "udp set failed" >&2; exit 1; }
        echo "applied roce udp port $port on chip $chip (was $orig)"
        ;;
    clean)
        if fault_present; then
            orig=$(sidecar_load rNPU_roce_port_change "$chip"); orig=${orig:-4791}
            $HCCN -udp -s port "$orig" || { echo "udp restore failed" >&2; exit 1; }
            sidecar_clear rNPU_roce_port_change "$chip"
            echo "restored roce udp port to $orig on chip $chip"
        else echo "udp port already at original, no-op"; fi
        ;;
    query) $HCCN -udp -g; fault_present ;;
esac
