#!/bin/sh

set -eu

log() {
    echo "[driver init] $*"
}

load_module() {
    module="$1"
    if [ -d "/sys/module/$module" ]; then
        log "$module already loaded"
        return 0
    fi

    log "Loading $module"
    modprobe "$module"
}

load_module mx6s_capture
load_module ov5640
load_module mpu6050

log "Drivers init done"