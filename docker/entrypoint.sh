#!/bin/bash
set -e

mkdir -p /app/logs

case "$1" in
    test)
        echo "[ENTRYPOINT] Running built-in self test..."
        exec ./pubsub_dsl test
        ;;
    listen)
        shift
        echo "[ENTRYPOINT] Starting node listener with args: $@"
        exec ./pubsub_dsl listen "$@"
        ;;
    run)
        shift
        echo "[ENTRYPOINT] Running script with args: $@"
        exec ./pubsub_dsl run "$@"
        ;;
    *)
        exec ./pubsub_dsl "$@"
        ;;
esac
