#!/bin/bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DIR="$PROJECT_ROOT/bin/data"

echo "=== KNOT Asset Setup ==="

mkdir -p "$DATA_DIR/fonts"
mkdir -p "$DATA_DIR/audio"
mkdir -p "$DATA_DIR/hrir"
mkdir -p "$DATA_DIR/shaders"
mkdir -p "$DATA_DIR/config"

HRTF_FILE="$DATA_DIR/hrir/mit_kemar_normal_pinna.sofa"
if [ ! -f "$HRTF_FILE" ]; then
    echo "Downloading MIT KEMAR HRTF..."
    if ! "$PROJECT_ROOT/scripts/download_hrtf.sh"; then
        echo "⚠️  HRTF download failed. Binaural rendering will be disabled until the file is provided."
    fi
else
    echo "HRTF already present: $HRTF_FILE"
fi

echo "Generating dummy bell.wav..."
python3 "$PROJECT_ROOT/scripts/generate_bell.py" "$DATA_DIR/audio/bell.wav"

echo "Copying scene timing config..."
cp "$PROJECT_ROOT/config/scene_timing.json" "$DATA_DIR/config/scene_timing.json"

cat <<'EOM'
✅ Asset setup complete.
⚠️ Replace dummy assets with production files when available:
   - bin/data/fonts/NotoSansJP-*.otf
   - bin/data/audio/bell.wav (recorded bell)
EOM
