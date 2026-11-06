#!/usr/bin/env bash
# Run from the root of your Sunshine fork:  bash /path/to/overlay/apply.sh
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
cd "$(git rev-parse --show-toplevel)"
cp -rv "$SRC"/. . | grep -v apply.sh || true
rm -f ./apply.sh
git rm -q --ignore-unmatch VR_WINDOW_ONLY.md WINDOW_ONLY_VR.md
echo "Done. Review with: git status && git diff"
