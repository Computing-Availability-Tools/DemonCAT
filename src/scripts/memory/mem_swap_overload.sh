#!/bin/sh
# rMEM_swap_overload: allocate size_mb (> free RAM) and dirty, forcing swap-out.
# inject: spawn a holder that allocates+touches size_mb; write pidfile
# clean:  kill holder; remove pidfile
# query:  check holder alive + free -m (Swap used)

PIDFILE="/tmp/dcat-rMEM_swap_overload.pid"

case "${DCAT_OP:-inject}" in
    inject)
        size=${DCAT_PARAM_SIZE_MB:?missing required param: size_mb}
        case "$size" in
            *[!0-9]*|"") echo "size_mb must be a positive integer, got: '$size'" >&2; exit 1;;
        esac

        if command -v perl >/dev/null 2>&1; then
            # allocate in chunks and touch each page by writing
            perl -e 'my $s=shift; my @a; my $chunk=16; for(my $i=0;$i<$s;$i+=$chunk){ push @a,"x"x($chunk*1024*1024) } select(undef,undef,undef,undef)' "$size" >/dev/null 2>&1 &
        elif command -v python3 >/dev/null 2>&1; then
            python3 -c '
import sys, time
s = int(sys.argv[1])
chunk = 16
a = []
i = 0
while i < s:
    a.append(b"x" * (chunk * 1024 * 1024))
    i += chunk
time.sleep(1e9)
' "$size" >/dev/null 2>&1 &
        else
            echo "neither perl nor python3 available" >&2; exit 1
        fi
        pid=$!
        echo "$pid" > "$PIDFILE"
        echo "swap overload driver started (pid $pid, size=${size} MB)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill "$pid" 2>/dev/null
            rm -f "$PIDFILE"
            echo "cleaned swap overload driver (pid $pid)"
        else
            echo "no active swap overload driver" >&2; exit 1
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "swap overload driver pid=$(cat "$PIDFILE")"
            echo "--- memory/swap ---"
            free -m 2>/dev/null | head -5 || cat /proc/meminfo 2>/dev/null | grep -i 'swap\|memfree\|memavailable'
            exit 0
        else
            echo "no active swap overload driver"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
