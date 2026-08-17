#!/bin/sh
# rDOCKER_kill: kill (stop) a docker container; clean restarts it.
# inject: docker kill <container>
# clean:  docker start <container> (glob all if no --container)
# query:  docker inspect State.Status (glob all if no --container)

SIDECAR_PFX="/tmp/dcat-rDOCKER_kill"

case "${DCAT_OP:-inject}" in
    inject)
        ctr=${DCAT_PARAM_CONTAINER:?missing required param: container}
        command -v docker >/dev/null 2>&1 || { echo "docker not installed" >&2; exit 1; }
        docker inspect "$ctr" >/dev/null 2>&1 || { echo "container $ctr not found" >&2; exit 1; }
        docker kill "$ctr" 2>/dev/null || { echo "docker kill failed" >&2; exit 1; }
        safe=$(echo "$ctr" | tr '/:' '__')
        printf '%s\n' "$ctr" > "${SIDECAR_PFX}-${safe}.sidecar"
        echo "killed container $ctr"
        ;;
    clean)
        ctr="${DCAT_PARAM_CONTAINER:-}"
        if [ -n "$ctr" ]; then
            safe=$(echo "$ctr" | tr '/:' '__')
            SIDECAR="${SIDECAR_PFX}-${safe}.sidecar"
            [ -f "$SIDECAR" ] || { echo "no active docker_kill for $ctr" >&2; exit 1; }
            docker start "$ctr" 2>/dev/null || true
            rm -f "$SIDECAR"
            echo "cleaned docker_kill (started $ctr)"
        else
            found=0
            for sf in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sf" ] || continue
                c=$(cat "$sf")
                docker start "$c" 2>/dev/null || true
                rm -f "$sf"
                found=1
            done
            [ "$found" = 1 ] && echo "cleaned all docker_kill" || { echo "no active docker_kill" >&2; exit 1; }
        fi
        ;;
    query)
        ctr="${DCAT_PARAM_CONTAINER:-}"
        if [ -n "$ctr" ]; then
            safe=$(echo "$ctr" | tr '/:' '__')
            st=$(docker inspect -f '{{.State.Status}}' "$ctr" 2>/dev/null)
            echo "container $ctr status=$st"
            [ "$st" = "exited" ] || [ "$st" = "dead" ]
        else
            found=0
            for sf in ${SIDECAR_PFX}-*.sidecar; do
                [ -f "$sf" ] || continue
                c=$(cat "$sf")
                st=$(docker inspect -f '{{.State.Status}}' "$c" 2>/dev/null)
                echo "container $c status=$st"
                found=1
            done
            if [ "$found" = 1 ]; then
                exit 0
            else
                echo "no active docker_kill"; exit 1
            fi
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
