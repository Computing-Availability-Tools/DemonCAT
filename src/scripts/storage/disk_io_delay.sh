#!/bin/sh
# rDISK_io_delay: device-mapper "delay" target over a block device.
# inject: create dm-delay device dcat-delay-<dev> wrapping <device>
# clean:  dmsetup remove
# query:  dmsetup info/table
# NOTE: applying to a mounted/in-use device is dangerous; test on spare devices.

SIDECAR="/tmp/dcat-rDISK_io_delay.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing required param: device}
        delay=${DCAT_PARAM_DELAY_MS:?missing required param: delay_ms}
        case "$delay" in *[!0-9]*|"") echo "delay_ms must be an integer" >&2; exit 1;; esac
        [ -b "$dev" ] || { echo "$dev is not a block device" >&2; exit 1; }
        devname=$(basename "$dev")
        dm="dcat-delay-${devname}"
        dmsetup info "$dm" >/dev/null 2>&1 && { echo "$dm already exists" >&2; exit 1; }
        size=$(blockdev --getsize "$dev" 2>/dev/null) || { echo "blockdev --getsize failed" >&2; exit 1; }
        echo "0 $size delay $dev 0 $delay" | dmsetup create "$dm" 2>/dev/null || { echo "dmsetup create failed (need root? dm-delay module?)" >&2; exit 1; }
        printf '%s\n' "$dm" > "$SIDECAR"
        echo "created dm-delay $dm over $dev (delay=${delay}ms)"
        ;;

    clean)
        [ -f "$SIDECAR" ] || { echo "no active io_delay" >&2; exit 1; }
        dm=$(cat "$SIDECAR")
        dmsetup remove "$dm" 2>/dev/null || dmsetup remove -f "$dm" 2>/dev/null || true
        rm -f "$SIDECAR"
        echo "cleaned io_delay (removed $dm)"
        ;;

    query)
        dm=$(cat "$SIDECAR" 2>/dev/null)
        if [ -n "$dm" ] && dmsetup info "$dm" >/dev/null 2>&1; then
            echo "dm-delay active: $dm"
            dmsetup table "$dm" 2>/dev/null
            exit 0
        else
            echo "no active io_delay"
            exit 1
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
