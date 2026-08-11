#!/bin/sh
# rDISK_loss: remove a SCSI disk (echo 1 > device/delete); clean rescans all hosts.
# inject: echo 1 > /sys/block/<dev>/device/delete
# clean:  echo "- - -" > /sys/class/scsi_host/host*/scan
# query:  check /sys/block/<dev> gone
# WARNING: dangerous — only on spare disks, never on root/root-vg disks.

SIDECAR="/tmp/dcat-rDISK_loss.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing required param: device}
        [ -b "$dev" ] || { echo "$dev is not a block device" >&2; exit 1; }
        devname=$(basename "$dev")
        delete="/sys/block/$devname/device/delete"
        [ -w "$delete" ] || { echo "$delete not writable (need root? not SCSI?)" >&2; exit 1; }
        echo 1 > "$delete" 2>/dev/null || { echo "disk delete failed" >&2; exit 1; }
        printf '%s\n' "$devname" > "$SIDECAR"
        echo "removed disk $devname (deleted)"
        ;;
    clean)
        [ -f "$SIDECAR" ] || { echo "no active disk_loss" >&2; exit 1; }
        devname=$(cat "$SIDECAR")
        for h in /sys/class/scsi_host/host*/scan; do
            [ -w "$h" ] && echo "- - -" > "$h" 2>/dev/null || true
        done
        rm -f "$SIDECAR"
        echo "cleaned disk_loss (rescanned scsi hosts to restore $devname)"
        ;;
    query)
        devname="${DCAT_PARAM_DEVICE:-}"
        [ -n "$devname" ] && devname=$(basename "$devname")
        if [ -z "$devname" ] && [ -f "$SIDECAR" ]; then devname=$(cat "$SIDECAR"); fi
        if [ -n "$devname" ] && [ ! -e "/sys/block/$devname" ]; then
            echo "disk $devname is gone (deleted)"
            exit 0
        else
            echo "disk ${devname:-unknown} present (not lost)"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
