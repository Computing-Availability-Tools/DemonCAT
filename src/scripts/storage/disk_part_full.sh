#!/bin/sh
# rDISK_part_full: fill a partition/mountpoint with a big file.
# size 支持单位: 100 (=100MB), 100M, 2G, 1G; 缺省则持续填充直至磁盘满(ENOSPC)
# inject: create a fill file (up to <size>, or until ENOSPC if size omitted)
# clean:  remove the fill file
# query:  show fill file size + df

SIDECAR_PFX="/tmp/dcat-rDISK_part_full"

# convert size string to MB count (for dd fallback); plain number = MB
size_to_mb() {
    s=$1
    case "$s" in
        *[0-9]G|*[0-9]g) num=${s%[Gg]}; echo $((num * 1024));;
        *[0-9]M|*[0-9]m) num=${s%[Mm]}; echo "$num";;
        *[0-9]K|*[0-9]k) num=${s%[Kk]}; echo $((num / 1024));;
        *[0-9]) echo "$s";;
        *) return 1;;
    esac
}

case "${DCAT_OP:-inject}" in
    inject)
        path=${DCAT_PARAM_PATH:?missing required param: path}
        size=${DCAT_PARAM_SIZE:-}
        [ -d "$path" ] || { echo "$path is not a directory" >&2; exit 1; }
        safe=$(echo "$path" | tr -c 'a-zA-Z0-9' '_')
        SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
        f="$path/dcat.fillfile.$$"
        if [ -n "$size" ]; then
            falloc_size=$size
            case "$size" in *[0-9]) falloc_size="${size}M";; esac
            if fallocate -l "$falloc_size" "$f" 2>/dev/null; then
                :
            else
                mb=$(size_to_mb "$size") || { echo "invalid size: $size" >&2; exit 1; }
                dd if=/dev/zero of="$f" bs=1M count="$mb" 2>/dev/null
            fi
        else
            # 无 size: 持续填充直至 ENOSPC
            dd if=/dev/zero of="$f" bs=1M 2>/dev/null || true
        fi
        printf '%s\n' "$f" > "$SIDECAR"
        echo "fill file created: $f (size=${size:-fill-to-full})"
        ;;
    clean)
        if [ -n "${DCAT_PARAM_PATH:-}" ]; then
            safe=$(echo "$DCAT_PARAM_PATH" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            if [ -f "$SIDECAR" ]; then
                f=$(cat "$SIDECAR")
                rm -f "$f"
                rm -f "$SIDECAR"
                echo "cleaned part_full (removed $f)"
            else
                echo "no active part_full" >&2; exit 1
            fi
        else
            cleaned=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                f=$(cat "$sc")
                rm -f "$f"
                rm -f "$sc"
                cleaned=1
            done
            if [ "$cleaned" = 1 ]; then
                echo "cleaned all part_full"
            else
                echo "no active part_full" >&2; exit 1
            fi
        fi
        ;;
    query)
        if [ -n "${DCAT_PARAM_PATH:-}" ]; then
            safe=$(echo "$DCAT_PARAM_PATH" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            f=$(cat "$SIDECAR" 2>/dev/null)
            if [ -n "$f" ] && [ -f "$f" ]; then
                sz=$(du -h "$f" 2>/dev/null | cut -f1)
                echo "fill file $f size=$sz"
                df -h "$DCAT_PARAM_PATH" 2>/dev/null | tail -1
                exit 0
            else
                echo "no active part_full"
                exit 1
            fi
        else
            active=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                f=$(cat "$sc" 2>/dev/null)
                if [ -n "$f" ] && [ -f "$f" ]; then
                    sz=$(du -h "$f" 2>/dev/null | cut -f1)
                    echo "fill file $f size=$sz"
                    active=1
                fi
            done
            if [ "$active" = 1 ]; then
                df -h 2>/dev/null | tail -1
                exit 0
            else
                echo "no active part_full"
                exit 1
            fi
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
