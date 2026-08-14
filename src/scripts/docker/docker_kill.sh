#!/bin/sh
# rDOCKER_kill: kill (stop) a docker container; clean restarts it.
# inject: docker kill <container>
# clean:  docker start <container>
# query:  docker inspect State.Status

SIDECAR="/tmp/dcat-rDOCKER_kill.sidecar"

case "${DCAT_OP:-inject}" in
    inject)
        ctr=${DCAT_PARAM_CONTAINER:?missing required param: container}
        command -v docker >/dev/null 2>&1 || { echo "docker not installed" >&2; exit 1; }
        docker inspect "$ctr" >/dev/null 2>&1 || { echo "container $ctr not found" >&2; exit 1; }
        docker kill "$ctr" 2>/dev/null || { echo "docker kill failed" >&2; exit 1; }
        printf '%s\n' "$ctr" > "$SIDECAR"
        echo "killed container $ctr"
        ;;
    clean)
        [ -f "$SIDECAR" ] || { echo "no active docker_kill" >&2; exit 1; }
        ctr=$(cat "$SIDECAR")
        docker start "$ctr" 2>/dev/null || true
        rm -f "$SIDECAR"
        echo "cleaned docker_kill (started $ctr)"
        ;;
    query)
        ctr="${DCAT_PARAM_CONTAINER:-$(cat "$SIDECAR" 2>/dev/null)}"
        [ -n "$ctr" ] || { echo "no container" >&2; exit 1; }
        st=$(docker inspect -f '{{.State.Status}}' "$ctr" 2>/dev/null)
        echo "container $ctr status=$st"
        [ "$st" = "exited" ] || [ "$st" = "dead" ]
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
