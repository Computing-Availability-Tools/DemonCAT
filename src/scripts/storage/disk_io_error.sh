#!/bin/sh
# rDISK_io_error: device-mapper "error" target — all IO returns EIO.
# inject: create dm-error device dcat-error-<dev> wrapping <device>
# clean:  dmsetup remove
# query:  dmsetup info/table
# NOTE: applying to a mounted/in-use device is dangerous; test on spare devices.

SIDECAR_PFX="/tmp/dcat-rDISK_io_error"

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing required param: device}
        [ -b "$dev" ] || { echo "$dev is not a block device" >&2; exit 1; }
        if findmnt "$dev" >/dev/null 2>&1; then
            echo "ERROR: $dev is mounted — refusing to create dm-error (would corrupt filesystem)" >&2; exit 1
        fi
        safe=$(echo "$dev" | tr -c 'a-zA-Z0-9' '_')
        SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
        devname=$(basename "$dev")
        dm="dcat-error-${devname}"
        if dmsetup info "$dm" >/dev/null 2>&1; then
            dmsetup remove -f "$dm" 2>/dev/null
            echo "removed stale $dm before inject" >&2
        fi
        size=$(blockdev --getsize "$dev" 2>/dev/null) || { echo "blockdev --getsize failed" >&2; exit 1; }
        echo "0 $size error" | dmsetup create "$dm" 2>/dev/null || { echo "dmsetup create failed (need root?)" >&2; exit 1; }
        dmsetup mknodes "$dm" 2>/dev/null || true
        printf '%s\n' "$dm" > "$SIDECAR"
        echo "created dm-error $dm over $dev (all IO returns EIO, dev node /dev/mapper/$dm)"
        ;;

    clean)
        if [ -n "${DCAT_PARAM_DEVICE:-}" ]; then
            safe=$(echo "$DCAT_PARAM_DEVICE" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            if [ -f "$SIDECAR" ]; then
                dm=$(cat "$SIDECAR")
                if dmsetup remove "$dm" 2>/dev/null || dmsetup remove -f "$dm" 2>/dev/null; then
                    rm -f "$SIDECAR"
                    echo "cleaned io_error (removed $dm)"
                else
                    echo "cleanup failed: dmsetup remove $dm (state preserved)" >&2; exit 1
                fi
            else
                echo "no active io_error" >&2; exit 0
            fi
        else
            cleaned=0
            failed=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                dm=$(cat "$sc")
                if dmsetup remove "$dm" 2>/dev/null || dmsetup remove -f "$dm" 2>/dev/null; then
                    rm -f "$sc"
                    cleaned=1
                else
                    echo "cleanup failed: dmsetup remove $dm (keeping $sc)" >&2
                    failed=1
                fi
            done
            if [ "$failed" = 1 ]; then
                echo "io_error: some cleanups failed (state preserved)" >&2; exit 1
            elif [ "$cleaned" = 1 ]; then
                echo "cleaned all io_error"
            else
                echo "no active io_error" >&2; exit 0
            fi
        fi
        ;;

    query)
        if [ -n "${DCAT_PARAM_DEVICE:-}" ]; then
            safe=$(echo "$DCAT_PARAM_DEVICE" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            dm=$(cat "$SIDECAR" 2>/dev/null)
            if [ -n "$dm" ] && dmsetup info "$dm" >/dev/null 2>&1; then
                echo "io_error: all IO to /dev/mapper/$dm returns EIO (Input/output error)"
                echo "  raw dm table: $(dmsetup table "$dm" 2>/dev/null)"
                exit 0
            else
                echo "no active io_error"; exit 1
            fi
        else
            active=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                dm=$(cat "$sc" 2>/dev/null)
                if [ -n "$dm" ] && dmsetup info "$dm" >/dev/null 2>&1; then
                    echo "io_error: /dev/mapper/$dm returns EIO"
                    active=1
                fi
            done
            [ "$active" = 1 ] && exit 0 || { echo "no active io_error"; exit 1; }
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
