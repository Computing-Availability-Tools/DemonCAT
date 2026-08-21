#!/bin/sh
# rFS_iowait_high: push up iowait% on a specific mount point / directory.
# inject: workers each loop dd bs=4k count=100 conv=fdatasync INTO <path>; write pidfile
# clean:  kill workers, rm temp dir under <path>
# query:  check workers alive + mpstat
# <path> 应为 mount 输出第 3 列的挂载点目录，iowait 计入该文件系统。

PIDFILE_PFX="/tmp/dcat-rFS_iowait_high"

case "${DCAT_OP:-inject}" in
    inject)
        path=${DCAT_PARAM_PATH:?missing required param: path}
        workers=${DCAT_PARAM_WORKERS:-4}
        case "$workers" in *[!0-9]*|"") echo "workers must be an integer" >&2; exit 1;; esac
        [ -d "$path" ] || { echo "$path is not a directory (should be a mount point)" >&2; exit 1; }
        safe=$(echo "$path" | tr -c 'a-zA-Z0-9' '_')
        PIDFILE="${PIDFILE_PFX}-${safe}.pid"
        target="$path/dcat.iowait.$$"
        mkdir -p "$target" || { echo "cannot mkdir $target" >&2; exit 1; }
        pids=""
        i=0
        while [ "$i" -lt "$workers" ]; do
            (
                trap 'kill $! 2>/dev/null; exit 0' TERM
                while true; do
                    dd if=/dev/zero of="$target/f.$i" bs=4k count=100 conv=fdatasync 2>/dev/null &
                    wait $!
                done
            ) >/dev/null 2>&1 &
            pids="$pids $!"
            i=$((i + 1))
        done
        printf '%s\n%s\n' "$pids" "$target" > "$PIDFILE"
        echo "iowait_high driver started ($workers workers, target=$target)"
        ;;
    clean)
        if [ -n "${DCAT_PARAM_PATH:-}" ]; then
            safe=$(echo "$DCAT_PARAM_PATH" | tr -c 'a-zA-Z0-9' '_')
            PIDFILE="${PIDFILE_PFX}-${safe}.pid"
            if [ -f "$PIDFILE" ]; then
                { read -r pids; read -r target; } < "$PIDFILE"
                _safe=0; case "$target" in *dcat.iowait*) _safe=1;; esac
                if [ "$_safe" = 0 ]; then echo "refusing rm -rf: unexpected path '$target'" >&2; rm -f "$PIDFILE"; exit 0; fi
                for pid in $pids; do kill -9 "$pid" 2>/dev/null; done
                pkill -9 -f "$target/" 2>/dev/null || true
                rm -rf "$target"
                rm -f "$PIDFILE"
                echo "cleaned iowait_high (removed $target)"
            else
                echo "no active iowait_high" >&2; exit 0
            fi
        else
            cleaned=0
            for pf in ${PIDFILE_PFX}-*.pid; do
                [ -f "$pf" ] || continue
                { read -r pids; read -r target; } < "$pf"
                for pid in $pids; do kill -9 "$pid" 2>/dev/null; done
                pkill -9 -f "$target/" 2>/dev/null || true
                rm -rf "$target"
                rm -f "$pf"
                cleaned=1
            done
            if [ "$cleaned" = 1 ]; then
                echo "cleaned all iowait_high"
            else
                echo "no active iowait_high" >&2; exit 0
            fi
        fi
        ;;
    query)
        if [ -n "${DCAT_PARAM_PATH:-}" ]; then
            safe=$(echo "$DCAT_PARAM_PATH" | tr -c 'a-zA-Z0-9' '_')
            PIDFILE="${PIDFILE_PFX}-${safe}.pid"
            [ -f "$PIDFILE" ] || { echo "no active iowait_high"; exit 1; }
            { read -r pids; } < "$PIDFILE"
            alive=0
            for pid in $pids; do kill -0 "$pid" 2>/dev/null && alive=$((alive + 1)); done
            echo "iowait_high workers alive=$alive"
            if command -v mpstat >/dev/null 2>&1; then
                mpstat 1 1 2>/dev/null | tail -3
            else
                top -bn1 2>/dev/null | head -5
            fi
            [ "$alive" -gt 0 ] && exit 0 || exit 1
        else
            alive=0
            for pf in ${PIDFILE_PFX}-*.pid; do
                [ -f "$pf" ] || continue
                { read -r pids; } < "$pf"
                for pid in $pids; do kill -0 "$pid" 2>/dev/null && alive=$((alive + 1)); done
            done
            echo "iowait_high workers alive=$alive"
            if [ "$alive" -gt 0 ]; then
                if command -v mpstat >/dev/null 2>&1; then
                    mpstat 1 1 2>/dev/null | tail -3
                else
                    top -bn1 2>/dev/null | head -5
                fi
                exit 0
            else
                echo "no active iowait_high"; exit 1
            fi
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
