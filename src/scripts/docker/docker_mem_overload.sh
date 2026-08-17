#!/bin/sh
# rDOCKER_mem_overload: allocate <size> RAM inside a container via docker exec.
# size 支持单位: 512 (=512MB), 512M, 2G, 1G, 256K
# inject: docker exec <container> <python3/perl holder> &; per-container pidfile
# clean:  kill host docker-exec PID + kill container-side holder (glob all if no --container)
# query:  check all active docker-exec PIDs + docker stats

PIDFILE_PFX="/tmp/dcat-rDOCKER_mem_overload"

case "${DCAT_OP:-inject}" in
    inject)
        ctr=${DCAT_PARAM_CONTAINER:?missing required param: container}
        size=${DCAT_PARAM_SIZE:?missing required param: size}
        command -v docker >/dev/null 2>&1 || { echo "docker not installed" >&2; exit 1; }
        docker inspect "$ctr" >/dev/null 2>&1 || { echo "container $ctr not found" >&2; exit 1; }

        safe=$(echo "$ctr" | tr '/:' '__')
        PIDFILE="${PIDFILE_PFX}-${safe}.pid"
        SIDECAR="${PIDFILE_PFX}-${safe}.sidecar"

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
        ctr="${DCAT_PARAM_CONTAINER:-}"
        if [ -n "$ctr" ]; then
            safe=$(echo "$ctr" | tr '/:' '__')
            PIDFILE="${PIDFILE_PFX}-${safe}.pid"
            SIDECAR="${PIDFILE_PFX}-${safe}.sidecar"
            if [ ! -f "$PIDFILE" ]; then
                echo "no active docker_mem_overload for $ctr" >&2; exit 1
            fi
            pid=$(cat "$PIDFILE")
            kill -9 "$pid" 2>/dev/null || true
            docker exec "$ctr" sh -c 'me=$$; for d in /proc/[0-9]*; do pid=${d##*/}; [ "$pid" = "$me" ] && continue; tr "\0" " " < "$d/cmdline" 2>/dev/null | grep -q "select.undef\|time.sleep" && kill -9 "$pid" 2>/dev/null; done' 2>/dev/null || true
            rm -f "$PIDFILE" "$SIDECAR"
            echo "cleaned docker_mem_overload (host exec pid=$pid, container=$ctr)"
        else
            found=0
            for pf in ${PIDFILE_PFX}-*.pid; do
                [ -f "$pf" ] || continue
                pid=$(cat "$pf")
                c=$(cat "${pf%.pid}.sidecar" 2>/dev/null)
                kill -9 "$pid" 2>/dev/null || true
                [ -n "$c" ] && docker exec "$c" sh -c 'me=$$; for d in /proc/[0-9]*; do pid=${d##*/}; [ "$pid" = "$me" ] && continue; tr "\0" " " < "$d/cmdline" 2>/dev/null | grep -q "select.undef\|time.sleep" && kill -9 "$pid" 2>/dev/null; done' 2>/dev/null || true
                rm -f "$pf" "${pf%.pid}.sidecar"
                found=1
            done
            [ "$found" = 1 ] && echo "cleaned all docker_mem_overload" || { echo "no active docker_mem_overload" >&2; exit 1; }
        fi
        ;;
    query)
        found=0
        for pf in ${PIDFILE_PFX}-*.pid; do
            [ -f "$pf" ] || continue
            pid=$(cat "$pf")
            if kill -0 "$pid" 2>/dev/null; then
                c=$(cat "${pf%.pid}.sidecar" 2>/dev/null)
                echo "docker_mem_overload host exec pid=$pid container=$c"
                [ -n "$c" ] && docker stats --no-stream --format "{{.MemUsage}}" "$c" 2>/dev/null
                found=1
            else
                rm -f "$pf" "${pf%.pid}.sidecar"
            fi
        done
        if [ "$found" = 1 ]; then
            exit 0
        else
            echo "no active docker_mem_overload"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
