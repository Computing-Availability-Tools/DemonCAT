#!/bin/sh
# config/scripts/process/proc_exit.sh
case "$DCAT_OP" in
    inject) echo "killed pid=$DCAT_PARAM_PID"; exit 0 ;;
    *)      echo "inject-only fault: op $DCAT_OP not supported" >&2; exit 1 ;;
esac
