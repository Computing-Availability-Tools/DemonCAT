#!/bin/sh
# rNET_conn_exhaust: hold N outbound TCP connections to a target.
# inject: spawn a socket holder; write pidfile
# clean:  kill holder
# query:  check holder alive + ss summary

PIDFILE="/tmp/dcat-rNET_conn_exhaust.pid"

case "${DCAT_OP:-inject}" in
    inject)
        target=${DCAT_PARAM_TARGET:?missing required param: target}
        count=${DCAT_PARAM_COUNT:-1000}
        case "$count" in *[!0-9]*|"") echo "count must be an integer" >&2; exit 1;; esac
        host=${target%%:*}
        port=${target##*:}
        [ -n "$host" ] && [ -n "$port" ] || { echo "target must be host:port" >&2; exit 1; }
        [ "$host" = "$port" ] && { echo "target must be host:port (missing : separator)" >&2; exit 1; }
        case "$port" in *[!0-9]*|"") echo "target port must be numeric" >&2; exit 1;; esac

        if command -v python3 >/dev/null 2>&1; then
            python3 -c '
import sys, socket, time
host, port, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
socks = []
for _ in range(n):
    try:
        s = socket.create_connection((host, port), timeout=3)
        socks.append(s)
    except OSError:
        break
time.sleep(1e9)
' "$host" "$port" "$count" >/dev/null 2>&1 &
        elif command -v perl >/dev/null 2>&1; then
            perl -e 'use IO::Socket::INET; my ($h,$p,$n)=@ARGV; my @s; for(1..$n){ my $c=IO::Socket::INET->new(PeerAddr=>"$h:$p",Timeout=>3) or last; push @s,$c } select(undef,undef,undef,undef)' "$host" "$port" "$count" >/dev/null 2>&1 &
        else
            echo "neither python3 nor perl available" >&2; exit 1
        fi
        pid=$!
        echo "$pid" > "$PIDFILE"
        echo "conn_exhaust driver started (pid $pid, target=$target count=$count)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill "$pid" 2>/dev/null
            rm -f "$PIDFILE"
            echo "cleaned conn_exhaust (pid $pid)"
        else
            echo "no active conn_exhaust" >&2; exit 1
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            pid=$(cat "$PIDFILE")
            fds=$(ls -1 /proc/$pid/fd 2>/dev/null | wc -l)
            echo "conn_exhaust driver pid=$pid (fds~$fds)"
            ss -s 2>/dev/null | head -5
            exit 0
        else
            echo "no active conn_exhaust"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
