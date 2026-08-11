#!/bin/sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
IMAGE_NAME="demoncat"

# ---- Detect docker compose command ----
if docker compose version >/dev/null 2>&1; then
    COMPOSE="docker compose"
elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE="docker-compose"
else
    COMPOSE=""
fi

# ---- Build image ----
echo "=== Building DemonCAT image ==="
docker build -f "$SCRIPT_DIR/Dockerfile" -t "$IMAGE_NAME" "$PROJECT_ROOT"
echo "Done. Image: $IMAGE_NAME"
echo ""

# ---- Auto-detect NPU ----
NPU_DETECTED=0
if [ -d /usr/local/Ascend/driver ]; then
    NPU_DETECTED=1
elif ls /dev/davinci* >/dev/null 2>&1; then
    NPU_DETECTED=1
fi

if [ "$NPU_DETECTED" -eq 1 ]; then
    echo "NPU detected (Ascend driver or /dev/davinci* found)"
else
    echo "No NPU detected, using generic mode"
fi

echo ""
echo "=== Quick start ==="
if [ -n "$COMPOSE" ]; then
    if [ "$NPU_DETECTED" -eq 1 ]; then
        echo "  Start web UI:  $COMPOSE -f $SCRIPT_DIR/docker-compose.yml -f $SCRIPT_DIR/docker-compose.npu.yml up -d"
    else
        echo "  Start web UI:  $COMPOSE -f $SCRIPT_DIR/docker-compose.yml up -d"
    fi
    echo "  Stop:          $COMPOSE -f $SCRIPT_DIR/docker-compose.yml down"
else
    echo "  (docker-compose not found, using docker run)"
    echo "  Start web UI:  docker run -d --name demoncat --privileged --network host --pid host demoncat serve --port 8080"
    if [ "$NPU_DETECTED" -eq 1 ]; then
        echo "  NPU:           docker run -d --name demoncat --privileged --network host --pid host \\"
        echo "                    -v /usr/local/Ascend/driver:/usr/local/Ascend/driver:ro \\"
        echo "                    -v /usr/local/Ascend/nnae:/usr/local/Ascend/nnae:ro \\"
        echo "                    --device /dev/davinci0 \\"
        echo "                    -e LD_LIBRARY_PATH=/usr/local/Ascend/driver/lib64/driver:/usr/local/Ascend/nnae/latest/lib64 \\"
        echo "                    -e PATH=/usr/local/Ascend/driver/tools:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \\"
        echo "                    demoncat serve --port 8080"
    fi
fi
echo "  Use CLI:       docker run --rm --privileged --network host --pid host $IMAGE_NAME <command>"
echo "                e.g. docker run --rm --privileged --network host --pid host $IMAGE_NAME list"
echo "  View web:      http://<server-ip>:8080"
