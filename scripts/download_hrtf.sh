#!/bin/bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DIR="$PROJECT_ROOT/bin/data"
DEST="$DATA_DIR/hrir/mit_kemar_normal_pinna.sofa"
SOURCE_URL="https://sofacoustics.org/data/database/mit/mit_kemar_normal_pinna.sofa"

mkdir -p "$(dirname "$DEST")"

if [ -f "$DEST" ]; then
    echo "HRTF already exists at $DEST"
    exit 0
fi

echo "Downloading MIT KEMAR HRTF to $DEST"
if command -v curl >/dev/null 2>&1; then
    curl -L -o "$DEST" "$SOURCE_URL"
elif command -v wget >/dev/null 2>&1; then
    wget -O "$DEST" "$SOURCE_URL"
else
    echo "Neither curl nor wget is available. Cannot download HRTF file."
    exit 1
fi

echo "Download complete: $DEST (size: $(du -h "$DEST" | cut -f1))"
