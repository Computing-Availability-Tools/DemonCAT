#!/bin/sh
# rDISK_part_full: fill a partition/mountpoint with a big file.
# size 支持单位: 100 (=100MB), 100M, 2G, 1G; 缺省则持续填充直至磁盘满(ENOSPC)
# inject: create a fill file (up to <size>, or until ENOSPC if size omitted)
# clean:  remove the fill file
# query:  show fill file size + df

SIDECAR="/tmp/dcat-rDISK_part_full.sidecar"

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
        f="$path/dcat.fillfile.$$"
        if [ -n "$size" ]; then
            # plain number → MB (preserve old behavior); suffixed passes through to fallocate
            falloc_size=$size
            case "$size" in *[0-9]) falloc_size="${size}M";; esac
            if fallocate -l "$falloc_size" "$f" 2>/dev/null; then
                :
            else
                mb=$(size_to_mb "$size") || { echo "invalid size: $size" >&2; exit 1; }
                dd if=/dev/zero of="$f" bs=1M count="$mb" 2>/dev/null
            fi
        else
            # no size: grow until ENOSPC
            fallocate -l 1T "$f" 2>/dev/null || dd if=/dev/zero of="$f" bs=1M 2>/dev/null || true
        fi
        printf '%s\n' "$f" > "$SIDECAR"
        echo "fill file created: $f (size=${size:-fill-to-full})"
        ;;
    clean)
        [ -f "$SIDECAR" ] || { echo "no active part_full" >&2; exit 1; }
        f=$(cat "$SIDECAR")
        rm -f "$f"
        rm -f "$SIDECAR"
        echo "cleaned part_full (removed $f)"
        ;;
    query)
        path=${DCAT_PARAM_PATH:-}
        f=$(cat "$SIDECAR" 2>/dev/null)
        if [ -n "$f" ] && [ -f "$f" ]; then
            sz=$(du -h "$f" 2>/dev/null | cut -f1)
            echo "fill file $f size=$sz"
            df -h "$path" 2>/dev/null | tail -1
            exit 0
        else
            echo "no active part_full"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
