#!/usr/bin/env bash
# Build the Python environment the analysis chain runs in. Run once.
#
#   ./tools/0-setup.sh
#
# Creates ../.venv and installs what the CSS scripts import. The scripts themselves are
# vendored at ../css-skill/ so there is nothing to clone; PINNED-COMMIT records which
# upstream commit that copy is.
#
# It installs python-can and python-can-cansub even though the offline chain never talks
# to their hardware, because it cannot start without them: css-skill's common.py imports
# python_can_cansub at module level, so every offline script dies on import if it is
# missing. Their own requirements.txt says these are "not needed for the offline-log
# workflow", and that is wrong. Both come from PyPI and need nothing plugged in.
#
# The OCR extras (rapidocr, onnxruntime, opencv) are NOT installed by default -- they are
# a few hundred MB and only the vision path uses them. Pass --with-vision for that.
set -euo pipefail
cd "$(dirname "$0")/.."

WITH_VISION=0
[[ "${1:-}" == "--with-vision" ]] && WITH_VISION=1

command -v python3 >/dev/null || { echo "python3 not found" >&2; exit 1; }

if [[ ! -d .venv ]]; then
    echo ">> creating .venv"
    python3 -m venv .venv
fi

echo ">> installing the analysis chain"
.venv/bin/pip -q install --upgrade pip
.venv/bin/pip -q install cantools numpy pandas scipy matplotlib python-can python-can-cansub

if (( WITH_VISION )); then
    echo ">> installing the vision extras (this one is large)"
    .venv/bin/pip -q install 'rapidocr>=2' onnxruntime opencv-python
fi

.venv/bin/python - <<'PY'
import cantools, numpy, pandas, scipy, python_can_cansub  # noqa: F401
print("   imports ok")
PY

cat <<'NEXT'

>> ready. Next:
     ./tools/1-selftest.sh      prove the whole chain on a virtual bus (no hardware)
NEXT
