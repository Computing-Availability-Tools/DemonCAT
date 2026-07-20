#!/bin/sh
# rPROC_exit: kill -9 a process (inject-only, irreversible).
# Env: DCAT_OP, DCAT_PARAM_PID
case "${DCAT_OP:-inject}" in
    inject)
        pid=${DCAT_PARAM_PID:?missing pid}
        if ! kill -9 "$pid" 2>&1; then
            echo "kill -9 $pid failed (no such process or no permission?)" >&2; exit 1
        fi
        echo "killed pid $pid"
        ;;
    clean)
        # inject-only: no clean op. If invoked, no-op (dcat rejects at precheck anyway).
        echo "rPROC_exit is inject-only; nothing to clean" >&2
        exit 0
        ;;
esac
