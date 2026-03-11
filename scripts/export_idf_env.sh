#!/usr/bin/env bash
set -euo pipefail

# Use ESP-IDF v5.5.2 by default for this repo.
IDF_DIR_DEFAULT="/Users/dev/.espressif/esp-idf-v5.5.2"
IDF_DIR="${ALFREDO_IDF_PATH:-$IDF_DIR_DEFAULT}"

if [[ ! -f "$IDF_DIR/export.sh" ]]; then
  echo "[ERROR] ESP-IDF export script not found: $IDF_DIR/export.sh" >&2
  echo "Set ALFREDO_IDF_PATH to your ESP-IDF root, then retry." >&2
  return 1 2>/dev/null || exit 1
fi

# Avoid stale env that points to removed Python envs (e.g. idf5.5_py3.9_env).
unset IDF_PYTHON_ENV_PATH

# shellcheck disable=SC1090
source "$IDF_DIR/export.sh"

echo "[OK] ESP-IDF environment loaded from: $IDF_DIR"
