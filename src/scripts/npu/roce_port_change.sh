#!/bin/sh
# rNPU_roce_port_change: RoCE UDP port change. Clean = restore original port (default 4791).
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
port=${DCAT_PARAM_PORT:-}
HCCN="hccn_tool -i $chip"

fault_present() {
    cur=$($HCCN -udp -g 2>/dev/null | grep -oE 'udp_port:[0-9]+' | grep -oE '[0-9]+')
    orig=$(sidecar_load rNPU_roce_port_change "$chip")
    [ -n "$cur" ] && [ -n "$orig" ] && [ "$cur" != "$orig" ]
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        : ${port:?missing required param: port}
        npu_check_env
        orig=$($HCCN -udp -g 2>/dev/null | grep -oE 'udp_port:[0-9]+' | grep -oE '[0-9]+')
        [ -n "$orig" ] && sidecar_save rNPU_roce_port_change "$chip" "$orig"
        $HCCN -udp -s port "$port" || { echo "udp set failed" >&2; exit 1; }
        echo "applied roce udp port $port on chip $chip (was $orig)"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            for bak in /tmp/dcat-rNPU_roce_port_change-*.bak; do
                [ -f "$bak" ] || continue
                c=${bak##*/dcat-rNPU_roce_port_change-}; c=${c%.bak}
                DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1 && cleaned=1
            done
            [ "$cleaned" = 1 ] && echo "restored roce udp port (all chips)" || echo "restored roce udp port (no active injection)"
        elif fault_present; then
            orig=$(sidecar_load rNPU_roce_port_change "$chip"); orig=${orig:-4791}
            $HCCN -udp -s port "$orig" || { echo "udp restore failed" >&2; exit 1; }
            sidecar_clear rNPU_roce_port_change "$chip"
            echo "restored roce udp port to $orig on chip $chip"
        else echo "udp port already at original, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -udp -g; fault_present && echo "FAULT CONFIRMED" || echo "FAULT NOT ACTIVE"' ;;
esac
