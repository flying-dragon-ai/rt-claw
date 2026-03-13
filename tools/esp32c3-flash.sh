#!/bin/bash
# Flash rt-claw to real ESP32-C3 hardware
#
# Usage: ./esp32c3-flash.sh [port]
#   port: serial port (default: /dev/ttyUSB0)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLATFORM_DIR="$(dirname "$SCRIPT_DIR")/platform/esp32c3-qemu"
PORT="${1:-/dev/ttyUSB0}"

cd "$PLATFORM_DIR" || exit 1

if [ ! -d "build" ]; then
    echo "Error: build/ not found. Build first:"
    echo "  cd platform/esp32c3-qemu"
    echo "  idf.py set-target esp32c3"
    echo "  idf.py build"
    exit 1
fi

exec idf.py -p "$PORT" flash monitor
