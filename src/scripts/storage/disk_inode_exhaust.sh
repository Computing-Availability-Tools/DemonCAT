#!/bin/sh
# rDISK_inode_exhaust: create many tiny files to exhaust inodes.
# inject: create count empty files under <path>/dcat.inodes.<pid>
# clean:  rm -rf the created inode dir
# query:  show file count + df -i

SIDECAR_PFX="/tmp/dcat-rDISK_inode_exhaust"

case "${DCAT_OP:-inject}" in
    inject)
        path=${DCAT_PARAM_PATH:?missing required param: path}
        count=${DCAT_PARAM_COUNT:-100000}
        case "$count" in *[!0-9]*|"") echo "count must be an integer" >&2; exit 1;; esac
        [ -d "$path" ] || { echo "$path is not a directory" >&2; exit 1; }
        safe=$(echo "$path" | tr -c 'a-zA-Z0-9' '_')
        SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
        dir="$path/dcat.inodes.$$"
        mkdir -p "$dir" || { echo "mkdir $dir failed" >&2; exit 1; }
        i=0; ok=0
        while [ "$i" -lt "$count" ]; do
            if : > "$dir/f$i" 2>/dev/null; then ok=$((ok + 1)); else break; fi
            i=$((i + 1))
        done
        printf '%s\n' "$dir" > "$SIDECAR"
        echo "created $ok files in $dir (requested $count)"
        ;;

    clean)
        if [ -n "${DCAT_PARAM_PATH:-}" ]; then
            safe=$(echo "$DCAT_PARAM_PATH" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            if [ -f "$SIDECAR" ]; then
                dir=$(cat "$SIDECAR")
                rm -rf "$dir"
                rm -f "$SIDECAR"
                echo "cleaned inode_exhaust (removed $dir)"
            else
                echo "no active inode_exhaust" >&2; exit 1
            fi
        else
            cleaned=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                dir=$(cat "$sc")
                rm -rf "$dir"
                rm -f "$sc"
                cleaned=1
            done
            if [ "$cleaned" = 1 ]; then
                echo "cleaned all inode_exhaust"
            else
                echo "no active inode_exhaust" >&2; exit 1
            fi
        fi
        ;;

    query)
        if [ -n "${DCAT_PARAM_PATH:-}" ]; then
            safe=$(echo "$DCAT_PARAM_PATH" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            dir=$(cat "$SIDECAR" 2>/dev/null)
            if [ -n "$dir" ] && [ -d "$dir" ]; then
                n=$(find "$dir" -type f 2>/dev/null | wc -l)
                echo "inode dir $dir: $n files"
                parent=$(dirname "$dir")
                df -i "$parent" 2>/dev/null | tail -1
                exit 0
            else
                echo "no active inode_exhaust"
                exit 1
            fi
        else
            active=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                dir=$(cat "$sc" 2>/dev/null)
                if [ -n "$dir" ] && [ -d "$dir" ]; then
                    n=$(find "$dir" -type f 2>/dev/null | wc -l)
                    echo "inode dir $dir: $n files"
                    active=1
                fi
            done
            if [ "$active" = 1 ]; then
                df -i 2>/dev/null | tail -1
                exit 0
            else
                echo "no active inode_exhaust"
                exit 1
            fi
        fi
        ;;

    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
