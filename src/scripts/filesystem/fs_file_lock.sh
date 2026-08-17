#!/bin/sh
# rFS_file_lock: lock a file for ALL users including root.
# noread:   mount --bind /dev/null (reads return empty for all users incl root)
# nowrite:  chmod a-w + chattr +i (writes fail for all users incl root)
# norw:     chattr +i + mount --bind -o ro <empty_file> (reads empty, writes fail EROFS)
# nodelete: chattr +i (delete/rename fail for all users incl root)
# Note: /dev/null is a device file — writes to it always "succeed" (discarded),
# so for norw we use a regular 0-byte file with -o ro to make writes fail.
# Directories: fall back to chmod + chattr (mount only works on files)
# inject: apply mode; save original state in sidecar
# clean:  umount (if mounted) + remove chattr + restore chmod
# query:  show current mode/attrs

SIDECAR_PFX="/tmp/dcat-rFS_file_lock"
EMPTY_RO="/tmp/dcat-empty-ro"

case "${DCAT_OP:-inject}" in
    inject)
        path=${DCAT_PARAM_PATH:?missing required param: path}
        mode=${DCAT_PARAM_MODE:?missing required param: mode}
        [ -e "$path" ] || { echo "$path does not exist" >&2; exit 1; }
        safe=$(echo "$path" | tr -c 'a-zA-Z0-9' '_')
        SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
        case "$mode" in
            noread|nowrite|norw|nodelete) ;;
            *) echo "mode must be one of: noread nowrite norw nodelete" >&2; exit 1;;
        esac
        orig_mode=$(stat -c %a "$path" 2>/dev/null)
        imm=0
        lsattr "$path" 2>/dev/null | grep -q 'i' && imm=1
        is_file=0
        [ -f "$path" ] && is_file=1
        mounted=0

        case "$mode" in
            noread)
                chmod a-r "$path" 2>/dev/null || { echo "chmod a-r failed" >&2; exit 1; }
                if [ "$is_file" = 1 ]; then
                    mount --bind /dev/null "$path" 2>/dev/null && mounted=1
                fi
                ;;
            nowrite)
                chmod a-w "$path" 2>/dev/null || { echo "chmod a-w failed" >&2; exit 1; }
                chattr +i "$path" 2>/dev/null || { echo "chattr +i failed (root bypasses chmod; chattr needs root + ext/xfs fs)" >&2; }
                ;;
            norw)
                chmod a-rw "$path" 2>/dev/null || { echo "chmod a-rw failed" >&2; exit 1; }
                chattr +i "$path" 2>/dev/null || { echo "chattr +i failed (root bypasses chmod; chattr needs root + ext/xfs fs)" >&2; }
                if [ "$is_file" = 1 ]; then
                    : > "$EMPTY_RO" 2>/dev/null
                    mount --bind -o ro "$EMPTY_RO" "$path" 2>/dev/null && mounted=1
                fi
                ;;
            nodelete)
                chattr +i "$path" 2>/dev/null || { echo "chattr +i failed (need root? not ext/xfs fs?)" >&2; exit 1; }
                ;;
        esac
        printf '%s\n%s\n%s\n%s\n' "$path" "$orig_mode" "$imm" "$mounted" > "$SIDECAR"
        echo "locked $path mode=$mode (was mode=$orig_mode imm=$imm mounted=$mounted)"
        ;;
    clean)
        if [ -n "${DCAT_PARAM_PATH:-}" ]; then
            safe=$(echo "$DCAT_PARAM_PATH" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            if [ -f "$SIDECAR" ]; then
                { read -r path; read -r orig_mode; read -r imm; read -r mounted; } < "$SIDECAR"
                if [ "$mounted" = 1 ]; then
                    umount "$path" 2>/dev/null || true
                fi
                if [ "$imm" = 0 ]; then
                    chattr -i "$path" 2>/dev/null || true
                fi
                [ -n "$orig_mode" ] && chmod "$orig_mode" "$path" 2>/dev/null || true
                rm -f "$SIDECAR"
                echo "cleaned file_lock (restored $path mode=$orig_mode imm=$imm mounted=$mounted)"
            else
                echo "no active file_lock" >&2; exit 1
            fi
        else
            cleaned=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                { read -r path; read -r orig_mode; read -r imm; read -r mounted; } < "$sc"
                if [ "$mounted" = 1 ]; then
                    umount "$path" 2>/dev/null || true
                fi
                if [ "$imm" = 0 ]; then
                    chattr -i "$path" 2>/dev/null || true
                fi
                [ -n "$orig_mode" ] && chmod "$orig_mode" "$path" 2>/dev/null || true
                rm -f "$sc"
                cleaned=1
            done
            if [ "$cleaned" = 1 ]; then
                echo "cleaned all file_lock"
            else
                echo "no active file_lock" >&2; exit 1
            fi
        fi
        ;;
    query)
        if [ -n "${DCAT_PARAM_PATH:-}" ]; then
            safe=$(echo "$DCAT_PARAM_PATH" | tr -c 'a-zA-Z0-9' '_')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            path="$DCAT_PARAM_PATH"
            mode_oct=$(stat -c %a "$path" 2>/dev/null)
            imm=$(lsattr "$path" 2>/dev/null | grep -q 'i' && echo yes || echo no)
            mnt=$(mount 2>/dev/null | grep -q "on $path " && echo yes || echo no)
            echo "file_lock: $path mode=$mode_oct immutable=$imm bind-mounted=$mnt"
            [ -f "$SIDECAR" ] && exit 0 || exit 1
        else
            active=0
            for sc in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sc" ] || continue
                { read -r path; } < "$sc"
                [ -n "$path" ] || continue
                mode_oct=$(stat -c %a "$path" 2>/dev/null)
                imm=$(lsattr "$path" 2>/dev/null | grep -q 'i' && echo yes || echo no)
                mnt=$(mount 2>/dev/null | grep -q "on $path " && echo yes || echo no)
                echo "file_lock: $path mode=$mode_oct immutable=$imm bind-mounted=$mnt"
                active=1
            done
            [ "$active" = 1 ] && exit 0 || exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
