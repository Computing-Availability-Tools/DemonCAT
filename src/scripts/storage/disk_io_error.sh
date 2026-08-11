#!/bin/sh
# rDISK_io_error: device-mapper "error" target — all IO returns EIO.
# inject: create dm-error device dcat-error-<dev> wrapping <device>
# clean:  dmsetup remove
# query:  dmsetup info/table
# NOTE: applying to a mounted/in-use device is dangerous; test on spare devices.

SIDECAR="/tmp/dcat-rDISK_io_error.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing required param: device}
        [ -b "$dev" ] || { echo "$dev is not a block device" >&2; exit 1; }
        devname=$(basename "$dev")
        dm="dcat-error-${devname}"
        dmsetup info "$dm" >/dev/null 2>&1 && { echo "$dm already exists" >&2; exit 1; }
        size=$(blockdev --getsize "$dev" 2>/dev/null) || { echo "blockdev --getsize failed" >&2; exit 1; }
        echo "0 $size error" | dmsetup create "$dm" 2>/dev/null || { echo "dmsetup create failed (need root?)" >&2; exit 1; }
        printf '%s\n' "$dm" > "$SIDECAR"
        echo "created dm-error $dm over $dev (all IO returns EIO)"
        ;;

    clean)
        [ -f "$SIDECAR" ] || { echo "no active io_error" >&2; exit 1; }
        dm=$(cat "$SIDECAR")
        dmsetup remove "$dm" 2>/dev/null || dmsetup remove -f "$dm" 2>/dev/null || true
        rm -f "$SIDECAR"
        echo "cleaned io_error (removed $dm)"
        ;;

    query)
        dm=$(cat "$SIDECAR" 2>/dev/null)
        if [ -n "$dm" ] && dmsetup info "$dm" >/dev/null 2>&1; then
            echo "dm-error active: $dm"
            dmsetup table "$dm" 2>/dev/null
            exit 0
        else
            echo "no active io_error"
            exit 1
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
