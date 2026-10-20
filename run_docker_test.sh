#!/usr/bin/env bash
set -e

echo "=========================================================="
echo " Building and Starting Docker PubSub Communication Test..."
echo "=========================================================="

DOCKER_COMPOSE_FILE="docker/docker-compose.yml"

# Clean up
docker compose -f "$DOCKER_COMPOSE_FILE" down -v --remove-orphans 2>/dev/null || true

# Run containers
echo ""
echo "[1/3] Building images and running multi-container scenario..."
docker compose -f "$DOCKER_COMPOSE_FILE" up --build --abort-on-container-exit --exit-code-from controller
EXIT_CODE=$?

echo ""
echo "[2/3] Inspecting Generated Log Files in ./logs/..."
if [ -f "logs/controller.log" ]; then
    echo "--- Controller Log Summary ---"
    tail -n 25 "logs/controller.log"
fi

if [ -f "logs/node_LAMPE.log" ]; then
    echo "--- Node LAMPE Received Events ---"
    tail -n 25 "logs/node_LAMPE.log"
fi

if [ -f "logs/node_VARME.log" ]; then
    echo "--- Node VARME Received Events ---"
    tail -n 25 "logs/node_VARME.log"
fi

echo ""
echo "[3/3] Cleaning up Docker containers..."
docker compose -f "$DOCKER_COMPOSE_FILE" down -v 2>/dev/null || true

if [ $EXIT_CODE -eq 0 ]; then
    echo ""
    echo "=========================================================="
    echo " DOCKER COMMUNICATION TEST COMPLETED SUCCESSFULLY! "
    echo "=========================================================="
else
    echo ""
    echo "=========================================================="
    echo " DOCKER COMMUNICATION TEST FAILED (Exit code: $EXIT_CODE)"
    echo "=========================================================="
fi

exit $EXIT_CODE
