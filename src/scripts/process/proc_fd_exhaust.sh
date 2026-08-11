#!/bin/sh
# rPROC_fd_exhaust: a single process opens fds until RLIMIT_NOFILE (or count).
# inject: spawn a process that opens /dev/null fds until limit; write pidfile
# clean:  kill process
# query:  check process alive + open fd count
# Distinct from system-wide fs.file-max lowering (per-process RLIMIT_NOFILE exhaustion).

PIDFILE="/tmp/dcat-rPROC_fd_exhaust.pid"

case "${DCAT_OP:-inject}" in
    inject)
        count=${DCAT_PARAM_COUNT:-0}   # 0 = until RLIMIT_NOFILE
        case "$count" in *[!0-9]*|"") echo "count must be an integer" >&2; exit 1;; esac
        if command -v python3 >/dev/null 2>&1; then
            python3 -c '
import sys, os, time
target = int(sys.argv[1])
fds = []
try:
    while True:
        if target and len(fds) >= target:
            break
        fds.append(os.open("/dev/null", os.O_RDONLY))
except OSError:
    pass
time.sleep(1e9)
' "$count" >/dev/null 2>&1 &
        elif command -v perl >/dev/null 2>&1; then
            perl -e 'my $t=shift; my @f; while(1){ last if $t && @f>=$t; open(my $h,"<","/dev/null") or last; push @f,$h } select(undef,undef,undef,undef)' "$count" >/dev/null 2>&1 &
        else
            echo "neither python3 nor perl available" >&2; exit 1
        fi
        pid=$!
        echo "$pid" > "$PIDFILE"
        echo "fd_exhaust driver started (pid $pid, count=$count)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill "$pid" 2>/dev/null
            rm -f "$PIDFILE"
            echo "cleaned fd_exhaust (pid $pid)"
        else
            echo "no active fd_exhaust" >&2; exit 1
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            pid=$(cat "$PIDFILE")
            n=$(ls -1 /proc/$pid/fd 2>/dev/null | wc -l)
            echo "fd_exhaust driver pid=$pid open_fds=$n"
            exit 0
        else
            echo "no active fd_exhaust"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
