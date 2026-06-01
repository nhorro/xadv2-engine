#!/usr/bin/env bash
#
# Start the local content-authoring stack for the game:
#   - MkDocs documentation on DOCS_PORT, default 8002
#   - room editor on ROOM_PORT, default 8000
#   - close-up editor on CLOSEUP_PORT, default 8001
#
# The script creates/uses VENV_DIR, installs missing Python dependencies, writes
# service logs to LOG_DIR, and stops all child services when one exits or when
# Ctrl-C is pressed.
#
# Customize by exporting environment variables before running it, for example:
#   ROOM_FILE=games/ingreso_urgente/data/rooms/lab.yaml ./run-devtools.sh
#   CLOSEUP_FILE=games/ingreso_urgente/data/closeups/window_llamas.yml ./run-devtools.sh
#   HOST=0.0.0.0 DOCS_PORT=9002 ROOM_PORT=9000 CLOSEUP_PORT=9001 ./run-devtools.sh
#
# Supported variables: HOST, DOCS_PORT, ROOM_PORT, CLOSEUP_PORT, GAME_DATA_DIR,
# ROOM_DIR, CLOSEUP_DIR, ROOM_FILE, CLOSEUP_FILE, VENV_DIR, and LOG_DIR.
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

HOST="${HOST:-127.0.0.1}"
DOCS_PORT="${DOCS_PORT:-8002}"
ROOM_PORT="${ROOM_PORT:-8000}"
CLOSEUP_PORT="${CLOSEUP_PORT:-8001}"

GAME_DATA_DIR="${GAME_DATA_DIR:-games/ingreso_urgente/data}"
ROOM_DIR="${ROOM_DIR:-${GAME_DATA_DIR}/rooms}"
CLOSEUP_DIR="${CLOSEUP_DIR:-${GAME_DATA_DIR}/closeups}"
ROOM_FILE="${ROOM_FILE:-${ROOM_DIR}/hall.yaml}"
CLOSEUP_FILE="${CLOSEUP_FILE:-${CLOSEUP_DIR}/lab_skull.yml}"

VENV_DIR="${VENV_DIR:-.venv}"
LOG_DIR="${LOG_DIR:-.devtools-logs}"
PYTHON="${VENV_DIR}/bin/python"

declare -a PIDS=()
declare -a LABELS=()
declare -a LOGS=()

usage() {
  cat <<EOF
Usage: $0

Starts the local authoring services:
  documentation    http://${HOST}:${DOCS_PORT}
  room editor      http://${HOST}:${ROOM_PORT}
  close-up editor  http://${HOST}:${CLOSEUP_PORT}

Configuration is via environment variables:
  HOST            default: ${HOST}
  DOCS_PORT       default: ${DOCS_PORT}
  ROOM_PORT       default: ${ROOM_PORT}
  CLOSEUP_PORT    default: ${CLOSEUP_PORT}
  GAME_DATA_DIR   default: ${GAME_DATA_DIR}
  ROOM_FILE       default: ${ROOM_FILE}
  CLOSEUP_FILE    default: ${CLOSEUP_FILE}
  VENV_DIR        default: ${VENV_DIR}
  LOG_DIR         default: ${LOG_DIR}
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

ensure_python_env() {
  if [[ ! -x "$PYTHON" ]]; then
    echo "Creating Python virtual environment at ${VENV_DIR}"
    python3 -m venv "$VENV_DIR"
  fi

  if ! "$PYTHON" -c "import mkdocs, yaml" >/dev/null 2>&1; then
    echo "Installing documentation and editor Python dependencies"
    "$PYTHON" -m pip install -r docs/requirements.txt PyYAML
  fi
}

require_file() {
  local label="$1"
  local path="$2"

  if [[ ! -f "$path" ]]; then
    echo "Missing ${label}: ${path}" >&2
    exit 1
  fi
}

start_service() {
  local label="$1"
  local log_file="$2"
  shift 2

  echo "Starting ${label}; log: ${log_file}"
  "$@" >"$log_file" 2>&1 &
  PIDS+=("$!")
  LABELS+=("$label")
  LOGS+=("$log_file")
}

cleanup() {
  local status=$?

  trap - EXIT INT TERM

  if ((${#PIDS[@]} > 0)); then
    echo
    echo "Stopping dev tools..."
    for pid in "${PIDS[@]}"; do
      kill "$pid" >/dev/null 2>&1 || true
    done
    for pid in "${PIDS[@]}"; do
      wait "$pid" >/dev/null 2>&1 || true
    done
  fi

  exit "$status"
}

trap cleanup EXIT INT TERM

ensure_python_env
require_file "room YAML" "$ROOM_FILE"
require_file "close-up YAML" "$CLOSEUP_FILE"
mkdir -p "$LOG_DIR"

start_service "documentation" "${LOG_DIR}/docs.log" \
  env PYTHONUNBUFFERED=1 "$PYTHON" -m mkdocs serve -a "${HOST}:${DOCS_PORT}"

start_service "room editor" "${LOG_DIR}/room-editor.log" \
  env PYTHONUNBUFFERED=1 "$PYTHON" -m tools.room_editor serve \
    --room "$ROOM_FILE" \
    --base-path "$ROOM_DIR" \
    --host "$HOST" \
    --port "$ROOM_PORT"

start_service "close-up editor" "${LOG_DIR}/closeup-editor.log" \
  env PYTHONUNBUFFERED=1 "$PYTHON" -m tools.closeup_editor serve \
    --closeup "$CLOSEUP_FILE" \
    --base-path "$GAME_DATA_DIR" \
    --host "$HOST" \
    --port "$CLOSEUP_PORT"

echo
echo "Dev tools are running:"
echo "  documentation    http://${HOST}:${DOCS_PORT}"
echo "  room editor      http://${HOST}:${ROOM_PORT}"
echo "  close-up editor  http://${HOST}:${CLOSEUP_PORT}"
echo
echo "Press Ctrl-C to stop all services."

set +e
wait -n "${PIDS[@]}"
EXITED_STATUS=$?
set -e

for i in "${!PIDS[@]}"; do
  if ! kill -0 "${PIDS[$i]}" >/dev/null 2>&1; then
    echo
    echo "${LABELS[$i]} exited. Recent log output:"
    tail -n 40 "${LOGS[$i]}" || true
    break
  fi
done

exit "$EXITED_STATUS"
