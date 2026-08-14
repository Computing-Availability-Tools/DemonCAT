#!/bin/sh
# rPROC_loop: spawn a process (optionally multi-threaded) in an infinite busy loop.
# inject: spawn loop driver; write pidfile
# clean:  kill driver
# query:  check driver alive + cpu/threads

PIDFILE="/tmp/dcat-rPROC_loop.pid"

case "${DCAT_OP:-inject}" in
    inject)
        threads=${DCAT_PARAM_THREADS:-1}
        case "$threads" in *[!0-9]*|"") echo "threads must be an integer" >&2; exit 1;; esac
        [ "$threads" -lt 1 ] && { echo "threads must be >= 1" >&2; exit 1; }

        if [ "$threads" -le 1 ]; then
            if command -v perl >/dev/null 2>&1; then
                perl -e '1 while 1' >/dev/null 2>&1 &
            else
                yes >/dev/null 2>&1 &
            fi
        else
            if command -v python3 >/dev/null 2>&1; then
                python3 -c '
import sys, threading, time
n = int(sys.argv[1])
def burn():
    while True:
        pass
for _ in range(n):
    threading.Thread(target=burn, daemon=True).start()
time.sleep(1e9)
' "$threads" >/dev/null 2>&1 &
            else
                echo "threads>1 requires python3 (for threading)" >&2; exit 1
            fi
        fi
        pid=$!
        echo "$pid" > "$PIDFILE"
        echo "loop driver started (pid $pid, threads=$threads)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill -9 "$pid" 2>/dev/null
            rm -f "$PIDFILE"
            echo "cleaned loop driver (pid $pid)"
        else
            echo "no active loop driver" >&2; exit 1
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            pid=$(cat "$PIDFILE")
            cpu=$(ps -o %cpu= -p "$pid" 2>/dev/null | tr -d ' ')
            nproc=$(ps -o nlwp= -p "$pid" 2>/dev/null | tr -d ' ')
            echo "loop driver pid=$pid cpu=$cpu threads=$nproc"
            exit 0
        else
            echo "no active loop driver"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
