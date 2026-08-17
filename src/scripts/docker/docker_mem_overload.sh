#!/bin/sh
# rDOCKER_mem_overload: allocate <size> RAM inside a container via docker exec.
# size 支持单位: 512 (=512MB), 512M, 2G, 1G, 256K
# inject: docker exec <container> <python3/perl holder> &; pidfile stores host docker-exec PID
# clean:  kill host docker-exec PID + kill container-side holder process
# query:  check docker-exec PID alive + docker stats

PIDFILE="/tmp/dcat-rDOCKER_mem_overload.pid"
SIDECAR="/tmp/dcat-rDOCKER_mem_overload.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        ctr=${DCAT_PARAM_CONTAINER:?missing required param: container}
        size=${DCAT_PARAM_SIZE:?missing required param: size}
        command -v docker >/dev/null 2>&1 || { echo "docker not installed" >&2; exit 1; }
        docker inspect "$ctr" >/dev/null 2>&1 || { echo "container $ctr not found" >&2; exit 1; }

        if docker exec "$ctr" sh -c 'command -v python3' >/dev/null 2>&1; then
            docker exec "$ctr" python3 -c '
import sys, time, re
s = sys.argv[1]
m = re.match(r"^\s*(\d+)\s*([KMG]?)\s*$", s, re.I)
if not m:
    raise SystemExit("bad size: %s (use e.g. 512, 512M, 2G)" % s)
n = int(m.group(1)); u = (m.group(2) or "M").upper()
mult = {"K": 1024, "M": 1024**2, "G": 1024**3}[u]
b = b"x" * (n * mult)
time.sleep(1e9)
' "$size" >/dev/null 2>&1 &
        elif docker exec "$ctr" sh -c 'command -v perl' >/dev/null 2>&1; then
            docker exec "$ctr" perl -e '
my $s = shift;
my ($n, $u) = $s =~ /^\s*(\d+)\s*([KMG]?)\s*$/i ? ($1, uc($2 || "M")) : die "bad size";
my %m = ("K", 1024, "M", 1024**2, "G", 1024**3);
my $b = "x" x ($n * $m{$u});
select(undef, undef, undef, undef);
' "$size" >/dev/null 2>&1 &
        else
            echo "container $ctr has neither python3 nor perl" >&2; exit 1
        fi
        pid=$!
        echo "$pid" > "$PIDFILE"
        printf '%s\n' "$ctr" > "$SIDECAR"
        echo "docker mem_overload started in $ctr (size=${size}, host exec pid=$pid)"
        ;;
    clean)
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill -9 "$pid" 2>/dev/null || true
            ctr=$(cat "$SIDECAR" 2>/dev/null)
            if [ -n "$ctr" ]; then
                docker exec "$ctr" sh -c 'me=$$; for d in /proc/[0-9]*; do pid=${d##*/}; [ "$pid" = "$me" ] && continue; tr "\0" " " < "$d/cmdline" 2>/dev/null | grep -q "select.undef\|time.sleep" && kill -9 "$pid" 2>/dev/null; done' 2>/dev/null || true
            fi
            rm -f "$PIDFILE" "$SIDECAR"
            echo "cleaned docker_mem_overload (host exec pid=$pid, container=$ctr)"
        else
            echo "no active docker_mem_overload" >&2; exit 1
        fi
        ;;
    query)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            pid=$(cat "$PIDFILE")
            echo "docker_mem_overload host exec pid=$pid"
            ctr=${DCAT_PARAM_CONTAINER:-$(cat "$SIDECAR" 2>/dev/null)}
            [ -n "$ctr" ] && docker stats --no-stream --format "{{.MemUsage}}" "$ctr" 2>/dev/null
            exit 0
        else
            echo "no active docker_mem_overload"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
