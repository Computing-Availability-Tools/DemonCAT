#!/bin/sh
# rDISK_scsi_error: inject disk IO errors via fail_make_request (debugfs).
# inject: echo 1 > /sys/block/<dev>/make-it-fail
# clean:  restore original
# query:  read make-it-fail
# Requires CONFIG_FAIL_MAKE_REQUEST + debugfs mounted.

SIDECAR="/tmp/dcat-rDISK_scsi_error.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing required param: device}
        devname=$(basename "$dev")
        f="/sys/block/$devname/make-it-fail"
        [ -f "$f" ] || { echo "$f not found (need CONFIG_FAIL_MAKE_REQUEST + debugfs)" >&2; exit 1; }
        orig=$(cat "$f" 2>/dev/null)
        echo 1 > "$f" 2>/dev/null || { echo "set make-it-fail failed (need root?)" >&2; exit 1; }
        printf '%s\n%s\n' "$devname" "$orig" > "$SIDECAR"
        echo "enabled make-it-fail on $devname"
        ;;
    clean)
        [ -f "$SIDECAR" ] || { echo "no active scsi_error" >&2; exit 1; }
        { read -r devname; read -r orig; } < "$SIDECAR"
        echo "${orig:-0}" > "/sys/block/$devname/make-it-fail" 2>/dev/null || true
        rm -f "$SIDECAR"
        echo "cleaned scsi_error (restored $devname make-it-fail=$orig)"
        ;;
    query)
        devname=""
        [ -n "${DCAT_PARAM_DEVICE:-}" ] && devname=$(basename "$DCAT_PARAM_DEVICE")
        if [ -z "$devname" ] && [ -f "$SIDECAR" ]; then read -r devname < "$SIDECAR"; fi
        f="/sys/block/$devname/make-it-fail"
        [ -f "$f" ] || { echo "make-it-fail unavailable"; exit 1; }
        cur=$(cat "$f" 2>/dev/null)
        echo "$f=$cur"
        [ "$cur" = "1" ]
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
