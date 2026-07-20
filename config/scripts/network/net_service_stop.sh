#!/bin/sh
# rNET_service_stop: stop a network service (dangerous).
# Env: DCAT_OP, DCAT_PARAM_SERVICE
SIDECAR=/run/demoncat/rNET_service_stop.svc
case "${DCAT_OP:-inject}" in
    inject)
        svc=${DCAT_PARAM_SERVICE:?missing service}
        if command -v systemctl >/dev/null 2>&1; then
            if ! systemctl stop "$svc" 2>&1; then
                echo "systemctl stop $svc failed" >&2; exit 1
            fi
        else
            # fallback: pkill the service by name
            if ! pkill -x "$svc" 2>&1; then
                echo "pkill $svc failed (no systemctl, no process?)" >&2; exit 1
            fi
        fi
        mkdir -p /run/demoncat 2>/dev/null
        echo "$svc" > "$SIDECAR" 2>/dev/null
        echo "stopped $svc"
        ;;
    clean)
        svc=${DCAT_PARAM_SERVICE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}
        [ -n "$svc" ] || { echo "no service to restart" >&2; exit 1; }
        if command -v systemctl >/dev/null 2>&1; then
            systemctl start "$svc" 2>/dev/null
        else
            # best-effort: service may need manual start
            (service "$svc" start 2>/dev/null || true)
        fi
        rm -f "$SIDECAR" 2>/dev/null
        echo "started $svc"
        ;;
    query)
        svc=${DCAT_PARAM_SERVICE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}
        if [ -z "$svc" ]; then
            echo "FAULT NOT ACTIVE: no service specified"
            exit 1
        fi
        if command -v systemctl >/dev/null 2>&1; then
            state=$(systemctl is-active "$svc" 2>/dev/null)
            if [ "$state" = "inactive" ] || [ "$state" = "failed" ] || [ "$state" = "deactivating" ]; then
                echo "FAULT CONFIRMED: service $svc is $state"
                systemctl status "$svc" 2>/dev/null | head -n 5
                exit 0
            else
                echo "FAULT NOT ACTIVE: service $svc is $state"
                exit 1
            fi
        else
            if pgrep -x "$svc" >/dev/null 2>&1; then
                echo "FAULT NOT ACTIVE: service $svc process is running"
                pgrep -lx "$svc" 2>/dev/null
                exit 1
            else
                echo "FAULT CONFIRMED: service $svc process not running"
                exit 0
            fi
        fi
        ;;
esac
