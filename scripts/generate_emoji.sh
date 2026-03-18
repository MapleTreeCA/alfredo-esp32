#!/usr/bin/env bash
# Generate Alfred emoji C sources from SVG faces, then rebuild assets.
# Usage: ./scripts/generate_emoji.sh
#
# Prerequisites: rsvg-convert (librsvg), Python 3, Pillow
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPONENT_DIR="$PROJECT_DIR/components/alfredo-fonts"

# ---------- Locate Python with Pillow ----------
PYTHON=""

# 1) ESP-IDF venv
for idf_venv in \
    "$HOME/.espressif/python_env"/idf*_env/bin/python3 \
    "$HOME/.espressif/python_env"/idf*_env/bin/python; do
    if [ -x "$idf_venv" ] 2>/dev/null; then
        if "$idf_venv" -c "from PIL import Image" 2>/dev/null; then
            PYTHON="$idf_venv"
            break
        fi
    fi
done

# 2) Homebrew / system python3
if [ -z "$PYTHON" ]; then
    for candidate in /opt/homebrew/bin/python3 /usr/local/bin/python3 python3; do
        if command -v "$candidate" &>/dev/null; then
            if "$candidate" -c "from PIL import Image" 2>/dev/null; then
                PYTHON="$candidate"
                break
            fi
        fi
    done
fi

if [ -z "$PYTHON" ]; then
    echo "ERROR: No Python with Pillow found. Install with: pip3 install Pillow" >&2
    exit 1
fi

# ---------- Check rsvg-convert ----------
if ! command -v rsvg-convert &>/dev/null; then
    echo "ERROR: rsvg-convert not found. Install with: brew install librsvg" >&2
    exit 1
fi

echo "Python : $PYTHON"
echo "rsvg   : $(command -v rsvg-convert)"
echo ""

# ---------- Run generator ----------
"$PYTHON" "$COMPONENT_DIR/generate_alfred_emoji.py"

# ---------- Clean up stray CMake artifacts ----------
# generate_alfred_emoji.py uses tmp/ for intermediates (in .gitignore).
# Remove any leftover build/ dir that would confuse ESP-IDF CMake.
rm -rf "$COMPONENT_DIR/build"

# Force assets partition rebuild on next idf.py build
rm -f "$PROJECT_DIR/build/generated_assets.bin"

echo ""
echo "Done. Run 'idf.py build && idf.py flash' to apply."
