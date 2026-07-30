#!/bin/sh
# rNET_service_stop: stop a network service (sync).
svc="${DCAT_PARAM_SERVICE:-}"
SIDECAR="/tmp/dcat-rNET_service_stop-${svc}.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        svc=${DCAT_PARAM_SERVICE:?missing required param: service}
        SIDECAR="/tmp/dcat-rNET_service_stop-${svc}.sidecar"
        if command -v systemctl >/dev/null 2>&1; then
            systemctl stop "$svc" || { echo "systemctl stop $svc failed" >&2; exit 1; }
        else
            pkill -x "$svc" || { echo "pkill $svc failed" >&2; exit 1; }
        fi
        echo "$svc" > "$SIDECAR"
        echo "stopped $svc"
        ;;
    clean)
        if [ -n "$DCAT_PARAM_SERVICE" ]; then
            svcs="$DCAT_PARAM_SERVICE"
        else
            svcs=""
            for sc in /tmp/dcat-rNET_service_stop-*.sidecar; do
                [ -f "$sc" ] || continue
                v=${sc##*/dcat-rNET_service_stop-}; v=${v%.sidecar}
                svcs="$svcs $v"
            done
        fi
        cleaned=0
        for svc in $svcs; do
            [ -n "$svc" ] || continue
            if command -v systemctl >/dev/null 2>&1; then
                systemctl start "$svc" 2>/dev/null
            else
                service "$svc" start 2>/dev/null || true
            fi
            rm -f "/tmp/dcat-rNET_service_stop-${svc}.sidecar"
            cleaned=1
        done
        if [ "$cleaned" = 1 ]; then echo "started [$svcs]";
        else echo "started (no active injection)"; fi
        ;;
    query)
        svc="${DCAT_PARAM_SERVICE:-$(cat "$SIDECAR" 2>/dev/null || echo "")}"
        if command -v systemctl >/dev/null 2>&1; then
            state=$(systemctl is-active "$svc" 2>/dev/null)
            echo "service=$svc state=$state"
            case "$state" in inactive|failed|deactivating) exit 0;; *) exit 1;; esac
        else
            pgrep -x "$svc" >/dev/null 2>&1 && exit 1 || exit 0
        fi
        ;;
esac
