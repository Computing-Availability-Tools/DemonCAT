#!/bin/sh
# rNPU_link_down: RoCE link down. Clean = -cfg recovery.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
[ -n "$chip" ] && npu_validate_chip "$chip"
HCCN="hccn_tool -i $chip"

fault_present() { $HCCN -link -g 2>/dev/null | grep -qi 'down'; }

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        echo y | $HCCN -link -s down || { echo "link down failed" >&2; exit 1; }
        echo "link down on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            echo "no active injection (chip required for link clean)"
        elif fault_present; then
            $HCCN -cfg recovery || { echo "cfg recovery failed" >&2; exit 1; }
            echo "restored config (link up) on chip $chip"
        else echo "link already up, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -link -g; fault_present' ;;
esac
