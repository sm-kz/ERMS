#!/bin/sh

set -eu

APP_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
WEB_DIR="$APP_DIR/web"

self_pid=""
httpd_pid=""

cleanup() {
    trap - TERM INT # 取消 SIGTERM、 SIGINT 的 trap
    echo "[gateway] Stopping services"

    if [ -n "$httpd_pid" ]; then
        kill "$httpd_pid" 2>/dev/null || true
    fi

    if [ -n "$self_pid" ]; then
        kill "$self_pid" 2>/dev/null || true
    fi

    wait 2>/dev/null || true
}

trap 'cleanup; exit 0' TERM INT
trap 'cleanup' EXIT

if [ -f "$APP_DIR/init.sh" ]; then
    "$APP_DIR/init.sh"
fi

echo "[gateway] Starting smart_gateway..."
"$APP_DIR/smart_gateway" &
self_pid=$!

echo "[gateway] Starting HTTP server..."
httpd -f -p 8000 -h "$WEB_DIR" &
httpd_pid=$!

wait "$self_pid"