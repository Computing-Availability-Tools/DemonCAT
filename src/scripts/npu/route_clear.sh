#!/bin/sh
# rNPU_route_clear: clear route table. Clean = -cfg recovery.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:?missing required param: chip}
npu_validate_chip "$chip"
HCCN="hccn_tool -i $chip"

fault_present() {
    cnt=$($HCCN -route -g 2>/dev/null | grep -cE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+')
    [ "$cnt" -eq 0 ] 2>/dev/null
}

case "${DCAT_OP:-inject}" in
    inject)
        npu_check_env
        $HCCN -route -c || { echo "route clear failed" >&2; exit 1; }
        echo "cleared route table on chip $chip"
        ;;
    clean)
        if fault_present; then
            $HCCN -cfg recovery || { echo "cfg recovery failed" >&2; exit 1; }
            echo "restored routes via cfg recovery on chip $chip"
        else echo "routes already present, no-op"; fi
        ;;
    query) $HCCN -route -g; fault_present ;;
esac
