#!/bin/sh
# rMEM_oom: unbounded memory allocation until OOM killer triggers.
# inject: spawn a driver that keeps allocating until killed by OOM; write pidfile
# clean:  kill driver (may already be OOM-killed); remove pidfile
# query:  check driver alive OR recent OOM-kill event in dmesg

PIDFILE="/tmp/dcat-rMEM_oom.pid"

case "${DCAT_OP:-inject}" in
    inject)
        rate=${DCAT_PARAM_RATE_MB:-64}
        case "$rate" in
            *[!0-9]*|"") echo "rate_mb must be a positive integer, got: '$rate'" >&2; exit 1;;
        esac
        [ "$rate" -lt 1 ] && { echo "rate_mb must be >= 1, got: '$rate'" >&2; exit 1; }

        if command -v perl >/dev/null 2>&1; then
            perl -e 'my $r=shift; my @a; while(1){ push @a,"x"x($r*1024*1024); select(undef,undef,undef,0.05) }' "$rate" >/dev/null 2>&1 &
        elif command -v python3 >/dev/null 2>&1; then
            python3 -c '
import sys, time
r = int(sys.argv[1])
a = []
while True:
    a.append(b"x" * (r * 1024 * 1024))
    time.sleep(0.05)
' "$rate" >/dev/null 2>&1 &
        else
            echo "neither perl nor python3 available" >&2; exit 1
        fi
        pid=$!
        echo "$pid" > "$PIDFILE"
        echo "oom driver started (pid $pid, rate ${rate} MB/step)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill -9 "$pid" 2>/dev/null
            rm -f "$PIDFILE"
            echo "cleaned oom driver (pid $pid)"
        else
            echo "no active oom driver" >&2; exit 0
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "oom driver running pid=$(cat "$PIDFILE")"
            dmesg 2>/dev/null | tail -50 | grep -i 'out of memory\|killed process' | tail -3
            exit 0
        else
            if dmesg 2>/dev/null | tail -200 | grep -qi 'out of memory\|killed process'; then
                echo "oom driver was OOM-killed (see dmesg)"
                dmesg 2>/dev/null | tail -200 | grep -i 'out of memory\|killed process' | tail -3
                exit 0
            fi
            echo "no active oom driver"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
