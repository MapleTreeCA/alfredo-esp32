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

# Prefer an existing ESP-IDF Python env for the same IDF major.minor series.
# This avoids export.sh selecting a missing env path when multiple Python
# versions are present locally.
IDF_SERIES="$(basename "$IDF_DIR" | sed -nE 's/^esp-idf-v([0-9]+\.[0-9]+).*/\1/p')"
if [[ -n "$IDF_SERIES" ]]; then
  PY_ENV_ROOT="$HOME/.espressif/python_env"
  if [[ -d "$PY_ENV_ROOT" ]]; then
    LATEST_MATCH="$(find "$PY_ENV_ROOT" -maxdepth 1 -type d -name "idf${IDF_SERIES}_py3.*_env" | sort -V | tail -n 1 || true)"
    if [[ -n "$LATEST_MATCH" ]]; then
      export IDF_PYTHON_ENV_PATH="$LATEST_MATCH"
    fi
  fi
fi

# shellcheck disable=SC1090
source "$IDF_DIR/export.sh"

echo "[OK] ESP-IDF environment loaded from: $IDF_DIR"
