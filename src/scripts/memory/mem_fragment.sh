#!/bin/sh
# rMEM_fragment: memory fragmentation — allocate N blocks, free every other, hold the rest.
# inject: spawn a driver that creates holes in userspace heap; write pidfile
# clean:  kill driver; remove pidfile
# query:  check driver alive + /proc/buddyinfo (kernel free-block distribution)

PIDFILE="/tmp/dcat-rMEM_fragment.pid"

case "${DCAT_OP:-inject}" in
    inject)
        block_kb=${DCAT_PARAM_BLOCK_KB:-1024}
        case "$block_kb" in *[!0-9]*|"") echo "block_kb must be a positive integer" >&2; exit 1;; esac
        [ "$block_kb" -lt 1 ] && { echo "block_kb must be >= 1" >&2; exit 1; }
        if [ -z "${DCAT_PARAM_BLOCKS:-}" ]; then
            free_mb=$(free -m | awk 'NR==2 {print $7}')
            blocks=$((free_mb > 1000 ? (free_mb - 1000) * 1024 / block_kb : 1))
        else
            blocks="$DCAT_PARAM_BLOCKS"
            case "$blocks" in *[!0-9]*|"") echo "blocks must be a positive integer" >&2; exit 1;; esac
            [ "$blocks" -lt 1 ] && { echo "blocks must be >= 1" >&2; exit 1; }
        fi

        if command -v perl >/dev/null 2>&1; then
            perl -e 'my ($n,$kb)=@ARGV; my @a; for(1..$n){ push @a,"x"x($kb*1024) } for(my $i=0;$i<@a;$i+=2){ $a[$i]=undef } select(undef,undef,undef,undef)' "$blocks" "$block_kb" >/dev/null 2>&1 &
        elif command -v python3 >/dev/null 2>&1; then
            python3 -c '
import sys, time
n, kb = int(sys.argv[1]), int(sys.argv[2])
a = [b"x" * (kb * 1024) for _ in range(n)]
for i in range(0, n, 2):
    a[i] = None
time.sleep(1e9)
' "$blocks" "$block_kb" >/dev/null 2>&1 &
        else
            echo "neither perl nor python3 available" >&2; exit 1
        fi
        pid=$!
        echo "$pid" > "$PIDFILE"
        sleep 1
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$PIDFILE"
            echo "fragmentation driver died immediately (allocation failed / OOM-killed)" >&2; exit 1
        fi
        echo "fragmentation driver started (pid $pid, blocks=$blocks bk=$block_kb)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill "$pid" 2>/dev/null
            rm -f "$PIDFILE"
            echo "cleaned fragmentation driver (pid $pid)"
        else
            echo "no active fragmentation driver" >&2; exit 0
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "fragmentation driver pid=$(cat "$PIDFILE")"
            echo "--- /proc/buddyinfo (free blocks per order) ---"
            cat /proc/buddyinfo 2>/dev/null | head -20
            exit 0
        else
            echo "no active fragmentation driver"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
