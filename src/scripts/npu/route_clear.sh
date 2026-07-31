#!/bin/sh
# rNPU_route_clear: clear route table. Clean = -cfg recovery.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
HCCN="hccn_tool -i $chip"

fault_present() {
    cnt=$($HCCN -route -g 2>/dev/null | grep -cE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+')
    [ "$cnt" -eq 0 ] 2>/dev/null
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        $HCCN -route -c || { echo "route clear failed" >&2; exit 1; }
        fault_present || { echo "rNPU_route_clear 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "cleared route table on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for route clean)"
        elif fault_present; then
            $HCCN -cfg recovery || { echo "cfg recovery failed" >&2; exit 1; }
            echo "restored routes via cfg recovery on chip $chip"
        else echo "routes already present, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -route -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
