#!/bin/sh -e
# Makes the RISC OS release zip, build/release/OpenTS-riscos-<version>.zip: !OpenTS (the game,
# with its help, licences and Prepare's helper, but no game data) and Prepare beside it.
# Filetypes are kept (see tools/riscos-zip.py). Run on the host, with Docker:
#   port/riscos/make-release.sh <version> [--no-build]
#   e.g. port/riscos/make-release.sh preview-20261007
VERSION=${1:?usage: make-release.sh <version> [--no-build]}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]; then
    echo "warning: uncommitted changes; the main menu will show a modified build" >&2
fi
if [ "${2:-}" != --no-build ]; then
    docker run --rm -v "$ROOT":/work opents-riscos /work/port/riscos/build.sh
fi
docker run --rm -v "$ROOT":/work opents-riscos /work/port/riscos/package.sh >/dev/null

OUT=$ROOT/build/release
mkdir -p "$OUT"
ZIP=$OUT/OpenTS-riscos-$VERSION.zip
rm -f "$ZIP"
python3 "$ROOT/port/riscos/tools/riscos-zip.py" "$ZIP" "$ROOT/build/riscos/!OpenTS" "$ROOT/build/riscos/Prepare,feb" >/dev/null
unzip -l "$ZIP" | tail -1
shasum -a 256 "$ZIP"
