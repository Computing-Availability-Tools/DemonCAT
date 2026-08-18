#!/bin/sh
# rDOCKER_mem_overload: allocate <size> RAM inside a container via docker exec.
# size 支持单位: 512 (=512MB), 512M, 2G, 1G, 256K
# inject: docker exec <container> <python3/perl holder> &; per-container pidfile+sidecar
# clean:  kill host docker-exec PID + kill container-side holder by exact PID (no grep)
# query:  check all active docker-exec PIDs + docker stats

PIDFILE_PFX="/tmp/dcat-rDOCKER_mem_overload"
MARKER="/tmp/dcat_mem_holder.pid"

case "${DCAT_OP:-inject}" in
    inject)
        ctr=${DCAT_PARAM_CONTAINER:?missing required param: container}
        size=${DCAT_PARAM_SIZE:?missing required param: size}
        command -v docker >/dev/null 2>&1 || { echo "docker not installed" >&2; exit 1; }
        docker inspect "$ctr" >/dev/null 2>&1 || { echo "container $ctr not found" >&2; exit 1; }

        safe=$(echo "$ctr" | tr '/:' '__')
        PIDFILE="${PIDFILE_PFX}-${safe}.pid"
        SIDECAR="${PIDFILE_PFX}-${safe}.sidecar"

        docker exec "$ctr" rm -f "$MARKER" 2>/dev/null || true
        if docker exec "$ctr" sh -c 'command -v python3' >/dev/null 2>&1; then
            docker exec "$ctr" python3 -c '
import sys, time, re, os
s = sys.argv[1]
m = re.match(r"^\s*(\d+)\s*([KMG]?)\s*$", s, re.I)
if not m:
    raise SystemExit("bad size: %s (use e.g. 512, 512M, 2G)" % s)
n = int(m.group(1)); u = (m.group(2) or "M").upper()
mult = {"K": 1024, "M": 1024**2, "G": 1024**3}[u]
open("'"$MARKER"'", "w").write(str(os.getpid()))
b = b"x" * (n * mult)
time.sleep(1e9)
' "$size" >/dev/null 2>&1 &
        elif docker exec "$ctr" sh -c 'command -v perl' >/dev/null 2>&1; then
            docker exec "$ctr" perl -e '
my $s = shift;
my ($n, $u) = $s =~ /^\s*(\d+)\s*([KMG]?)\s*$/i ? ($1, uc($2 || "M")) : die "bad size";
my %m = ("K", 1024, "M", 1024**2, "G", 1024**3);
open(my $fh, ">", "'"$MARKER"'") or die;
print $fh $$;
close $fh;
my $b = "x" x ($n * $m{$u});
select(undef, undef, undef, undef);
' "$size" >/dev/null 2>&1 &
        else
            echo "container $ctr has neither python3 nor perl" >&2; exit 1
        fi
        pid=$!
        sleep 0.5
        ctr_pid=$(docker exec "$ctr" cat "$MARKER" 2>/dev/null)
        echo "$pid" > "$PIDFILE"
        printf '%s\n%s\n' "$ctr" "${ctr_pid:-}" > "$SIDECAR"
        echo "docker mem_overload started in $ctr (size=${size}, host pid=$pid, ctr pid=${ctr_pid:-unknown})"
        ;;
    clean)
        ctr="${DCAT_PARAM_CONTAINER:-}"
        if [ -n "$ctr" ]; then
            safe=$(echo "$ctr" | tr '/:' '__')
            PIDFILE="${PIDFILE_PFX}-${safe}.pid"
            SIDECAR="${PIDFILE_PFX}-${safe}.sidecar"
            if [ ! -f "$PIDFILE" ]; then
                echo "no active docker_mem_overload for $ctr" >&2; exit 0
            fi
            pid=$(cat "$PIDFILE")
            ctr_pid=$(sed -n '2p' "$SIDECAR" 2>/dev/null)
            kill -9 "$pid" 2>/dev/null || true
            [ -n "$ctr_pid" ] && docker exec "$ctr" kill -9 "$ctr_pid" 2>/dev/null || true
            docker exec "$ctr" rm -f "$MARKER" 2>/dev/null || true
            rm -f "$PIDFILE" "$SIDECAR"
            echo "cleaned docker_mem_overload (host pid=$pid, ctr pid=${ctr_pid:-none}, container=$ctr)"
        else
            found=0
            for pf in ${PIDFILE_PFX}-*.pid; do
                [ -f "$pf" ] || continue
                pid=$(cat "$pf")
                c=$(sed -n '1p' "${pf%.pid}.sidecar" 2>/dev/null)
                cp=$(sed -n '2p' "${pf%.pid}.sidecar" 2>/dev/null)
                kill -9 "$pid" 2>/dev/null || true
                [ -n "$c" ] && [ -n "$cp" ] && docker exec "$c" kill -9 "$cp" 2>/dev/null || true
                [ -n "$c" ] && docker exec "$c" rm -f "$MARKER" 2>/dev/null || true
                rm -f "$pf" "${pf%.pid}.sidecar"
                found=1
            done
            [ "$found" = 1 ] && echo "cleaned all docker_mem_overload" || { echo "no active docker_mem_overload" >&2; exit 0; }
        fi
        ;;
    query)
        found=0
        for pf in ${PIDFILE_PFX}-*.pid; do
            [ -f "$pf" ] || continue
            pid=$(cat "$pf")
            if kill -0 "$pid" 2>/dev/null; then
                c=$(sed -n '1p' "${pf%.pid}.sidecar" 2>/dev/null)
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
