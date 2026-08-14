#!/bin/sh
# rFS_file_lock: lock a file — noread/nowrite/norw (chmod) or nodelete (chattr +i).
# inject: apply mode; save original mode + immutable state in sidecar
# clean:  restore original mode + remove immutable if we added it
# query:  show current mode/attrs

SIDECAR="/tmp/dcat-rFS_file_lock.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        path=${DCAT_PARAM_PATH:?missing required param: path}
        mode=${DCAT_PARAM_MODE:?missing required param: mode}
        [ -e "$path" ] || { echo "$path does not exist" >&2; exit 1; }
        case "$mode" in
            noread|nowrite|norw|nodelete) ;;
            *) echo "mode must be one of: noread nowrite norw nodelete" >&2; exit 1;;
        esac
        orig_mode=$(stat -c %a "$path" 2>/dev/null)
        imm=0
        lsattr "$path" 2>/dev/null | grep -q 'i' && imm=1
        case "$mode" in
            noread)   chmod a-r "$path" 2>/dev/null || { echo "chmod a-r failed" >&2; exit 1; };;
            nowrite)  chmod a-w "$path" 2>/dev/null || { echo "chmod a-w failed" >&2; exit 1; };;
            norw)     chmod a-rw "$path" 2>/dev/null || { echo "chmod a-rw failed" >&2; exit 1; };;
            nodelete) chattr +i "$path" 2>/dev/null || { echo "chattr +i failed (need root? not ext fs?)" >&2; exit 1; };;
        esac
        printf '%s\n%s\n%s\n' "$path" "$orig_mode" "$imm" > "$SIDECAR"
        echo "locked $path mode=$mode (was mode=$orig_mode imm=$imm)"
        ;;
    clean)
        [ -f "$SIDECAR" ] || { echo "no active file_lock" >&2; exit 1; }
        { read -r path; read -r orig_mode; read -r imm; } < "$SIDECAR"
        if [ "$imm" = 0 ]; then
            chattr -i "$path" 2>/dev/null || true
        fi
        [ -n "$orig_mode" ] && chmod "$orig_mode" "$path" 2>/dev/null || true
        rm -f "$SIDECAR"
        echo "cleaned file_lock (restored $path mode=$orig_mode imm=$imm)"
        ;;
    query)
        path="${DCAT_PARAM_PATH:-}"
        if [ -z "$path" ] && [ -f "$SIDECAR" ]; then read -r path < "$SIDECAR"; fi
        if [ -z "$path" ]; then echo "no path" >&2; exit 1; fi
        stat -c '%A %a %n' "$path" 2>/dev/null
        lsattr "$path" 2>/dev/null
        [ -f "$SIDECAR" ] && exit 0 || exit 1
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
