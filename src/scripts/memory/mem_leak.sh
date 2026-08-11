#!/bin/sh
# rMEM_leak: hold size_mb of memory without freeing (memory leak simulation).
# inject: spawn a holder that allocates size_mb and blocks; write pidfile; return
# clean:  read pidfile, kill holder, remove pidfile
# query:  check holder alive + its RSS

PIDFILE="/tmp/dcat-rMEM_leak.pid"

case "${DCAT_OP:-inject}" in
    inject)
        size=${DCAT_PARAM_SIZE_MB:?missing required param: size_mb}
        case "$size" in
            *[!0-9]*|"") echo "size_mb must be a positive integer, got: '$size'" >&2; exit 1;;
        esac
        [ "$size" -lt 1 ] && { echo "size_mb must be >= 1, got: '$size'" >&2; exit 1; }

        if command -v perl >/dev/null 2>&1; then
            perl -e 'my $s=shift; my $b="x"x($s*1024*1024); select(undef,undef,undef,undef)' "$size" >/dev/null 2>&1 &
        elif command -v python3 >/dev/null 2>&1; then
            python3 -c 'import sys,time; b=b"x"*(int(sys.argv[1])*1024*1024); time.sleep(1e9)' "$size" >/dev/null 2>&1 &
        else
            echo "neither perl nor python3 available" >&2; exit 1
        fi
        pid=$!
        echo "$pid" > "$PIDFILE"
        echo "leaked ${size} MB (pid $pid)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill "$pid" 2>/dev/null
            rm -f "$PIDFILE"
            echo "cleaned memory leak (pid $pid)"
        else
            echo "no active memory leak" >&2; exit 1
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            pid=$(cat "$PIDFILE")
            rss=$(ps -o rss= -p "$pid" 2>/dev/null | tr -d ' ')
            echo "leak holder pid=$pid rss=${rss}KB"
            exit 0
        else
            echo "no active memory leak"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
