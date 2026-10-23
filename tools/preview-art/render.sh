#!/usr/bin/env bash
# tools/preview-art/render.sh - SVG art for the Settings previews -> PNG in assets/icons.
# rsvg-convert lives in the dev container; re-runs itself there from the host.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if ! command -v rsvg-convert >/dev/null 2>&1; then
    cd "${ROOT}"
    MSYS_NO_PATHCONV=1 exec docker compose run --rm ps5-dev bash ./tools/preview-art/render.sh
fi
rsvg-convert -w 720 "${ROOT}/tools/preview-art/controller.svg" \
    -o "${ROOT}/projects/evoplayer/assets/icons/preview_controller.png"
echo "wrote projects/evoplayer/assets/icons/preview_controller.png"
