#!/bin/sh
# rNPU_link_down: RoCE link down. Clean = -cfg recovery.
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
HCCN="hccn"

fault_present() { $HCCN -link -g 2>/dev/null | grep -qi 'down'; }

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        npu_check_env
        echo y | $HCCN -link -s down || { echo "link down failed" >&2; exit 1; }
        fault_present || { echo "rNPU_link_down 注入回读校验失败:动作未生效" >&2; exit 1; }
        echo "link down on chip $chip"
        ;;
    clean)
        if [ -z "$chip" ]; then
            cleaned=0
            failed=0
            for c in $(npu_list_chips); do
                _oc=$chip; _oh=$HCCN
                chip=$c; HCCN="hccn"
                if fault_present; then
                    if DCAT_OP=clean DCAT_PARAM_CHIP="$c" "$0" >/dev/null 2>&1; then
                        cleaned=1
                    else
                        echo "restore failed for chip $c" >&2
                        failed=1
                    fi
                fi
                chip=$_oc; HCCN=$_oh
            done
            if [ "$failed" = 1 ]; then
                echo "link_down: some restores failed (state preserved)" >&2; exit 1
            elif [ "$cleaned" = 1 ]; then
                echo "restored link (all chips)"
            else
                echo "restored link (no active injection)"
            fi
        elif fault_present; then
            $HCCN -cfg recovery || { echo "cfg recovery failed" >&2; exit 1; }
            echo "restored config (link up) on chip $chip"
        else echo "link already up, no-op"; fi
        ;;
    query) npu_foreach_chip '$HCCN -link -g; fault_present && echo "FAULT CONFIRMED" || { echo "FAULT NOT ACTIVE"; false; }' ;;
esac
