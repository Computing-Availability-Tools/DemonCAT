#!/bin/sh
# rDISK_io_delay: device-mapper "delay" target over a block device.
# inject: create dm-delay device dcat-delay-<dev> wrapping <device>
# clean:  dmsetup remove
# query:  dmsetup info/table
# NOTE: applying to a mounted/in-use device is dangerous; test on spare devices.

SIDECAR_PFX="/tmp/dcat-rDISK_io_delay"

case "${DCAT_OP:-inject}" in
    inject)
        dev=${DCAT_PARAM_DEVICE:?missing required param: device}
        delay=${DCAT_PARAM_DELAY_MS:?missing required param: delay_ms}
        case "$delay" in *[!0-9]*|"") echo "delay_ms must be an integer" >&2; exit 1;; esac
        [ -b "$dev" ] || { echo "$dev is not a block device" >&2; exit 1; }
        safe=$(echo "$dev" | tr -c 'a-zA-Z0-9' '_')
        SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
        devname=$(basename "$dev")
        dm="dcat-delay-${devname}"
        dmsetup info "$dm" >/dev/null 2>&1 && { echo "$dm already exists" >&2; exit 1; }
        size=$(blockdev --getsize "$dev" 2>/dev/null) || { echo "blockdev --getsize failed" >&2; exit 1; }
        echo "0 $size delay $dev 0 $delay" | dmsetup create "$dm" 2>/dev/null || { echo "dmsetup create failed (need root? dm-delay module?)" >&2; exit 1; }
        dmsetup mknodes "$dm" 2>/dev/null || true
        printf '%s\n' "$dm" > "$SIDECAR"
        echo "created dm-delay $dm over $dev (delay=${delay}ms, dev node /dev/mapper/$dm)"
        ;;

    clean)
        if [ -n "${DCAT_PARAM_DEVICE:-}" ]; then
            safe=$(echo "$DCAT_PARAM_DEVICE" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            if [ -f "$SIDECAR" ]; then
                dm=$(cat "$SIDECAR")
                if dmsetup remove "$dm" 2>/dev/null || dmsetup remove -f "$dm" 2>/dev/null; then
                    rm -f "$SIDECAR"
                    echo "cleaned io_delay (removed $dm)"
                else
                    echo "cleanup failed: dmsetup remove $dm (state preserved)" >&2; exit 1
                fi
            else
                echo "no active io_delay" >&2; exit 0
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
                echo "io_delay: some cleanups failed (state preserved)" >&2; exit 1
            elif [ "$cleaned" = 1 ]; then
                echo "cleaned all io_delay"
            else
                echo "no active io_delay" >&2; exit 0
            fi
        fi
        ;;

    query)
        if [ -n "${DCAT_PARAM_DEVICE:-}" ]; then
            safe=$(echo "$DCAT_PARAM_DEVICE" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            dm=$(cat "$SIDECAR" 2>/dev/null)
            if [ -n "$dm" ] && dmsetup info "$dm" >/dev/null 2>&1; then
                tbl=$(dmsetup table "$dm" 2>/dev/null)
                delay_ms=$(echo "$tbl" | awk '{print $NF}')
                echo "io_delay: ${delay_ms}ms delay active — every IO to /dev/mapper/$dm is delayed"
                echo "  raw dm table: $tbl"
                exit 0
            else
                echo "no active io_delay"; exit 1
            fi
        else
            active=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                dm=$(cat "$sc" 2>/dev/null)
                if [ -n "$dm" ] && dmsetup info "$dm" >/dev/null 2>&1; then
                    tbl=$(dmsetup table "$dm" 2>/dev/null)
                    delay_ms=$(echo "$tbl" | awk '{print $NF}')
                    echo "io_delay: ${delay_ms}ms delay on /dev/mapper/$dm"
                    active=1
                fi
            done
            [ "$active" = 1 ] && exit 0 || { echo "no active io_delay"; exit 1; }
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
