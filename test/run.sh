#!/bin/bash
# Запуск PID Controller с правильным venv
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV="$SCRIPT_DIR/../.venv/bin/python"
exec "$VENV" "$SCRIPT_DIR/pid_controller.py" "$@"
