#!/bin/sh -e
# Makes the RISC OS release zip, build/release/OpenTS-riscos-<version>.zip: !OpenTS (the game,
# with its help, licences and installer, but no game data).
# Filetypes are kept (see tools/riscos-zip.py). Run on the host, with Docker:
#   port/riscos/make-release.sh <version>
#   e.g. port/riscos/make-release.sh 0.1.0
#
# <version> must be the one in port/VERSION, and the build must be the release of it: from a clean
# tree (changes inside submodules aside: the build patches SDL) whose HEAD is exactly the commit
# that the annotated tag riscos-v<version> points at. Otherwise this stops, and says why. The
# program, its Info window, !Help and its log then say exactly <version>.
#
# OPENTS_DEV_BUILD=1 makes a zip of any other state instead, named OpenTS-riscos-<version>-dev-
# <commit>[-modified].zip, whose program says <version>-dev and names the commit it was built from.
VERSION=${1:?usage: make-release.sh <version>   (see the notes at the top of this script)}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
FILE_VERSION=$(head -n 1 "$ROOT/port/VERSION" | tr -d '[:space:]')
TAG=riscos-v$VERSION
fail() {
    echo "make-release.sh: $*" >&2
    exit 1
}

[ "${2:-}" != --no-build ] || fail "--no-build is gone: a zip must come from a build of this very commit"
[ "$VERSION" = "$FILE_VERSION" ] || fail "asked for $VERSION but port/VERSION says $FILE_VERSION. Bump port/VERSION (and commit it) first, or ask for $FILE_VERSION."

HEAD_COMMIT=$(git -C "$ROOT" rev-parse HEAD)
SHORT=$(git -C "$ROOT" rev-parse --short HEAD)
MODIFIED=
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no --ignore-submodules=dirty)" ]; then
    MODIFIED=1
fi
TAG_COMMIT=$(git -C "$ROOT" rev-parse -q --verify "refs/tags/$TAG^{commit}" || true)

PROBLEM=
if [ -n "$MODIFIED" ]; then
    PROBLEM="the tree has uncommitted changes (see git status); commit them first"
elif [ -z "$TAG_COMMIT" ]; then
    PROBLEM="there is no tag $TAG. Tag the release commit: git tag -a $TAG -m 'OpenTS $VERSION for RISC OS'"
elif [ "$TAG_COMMIT" != "$HEAD_COMMIT" ]; then
    PROBLEM="HEAD ($SHORT) is not the commit $TAG points at ($(git -C "$ROOT" rev-parse --short "$TAG_COMMIT")). Check out the tag, or move on to the next version"
fi

if [ -n "$PROBLEM" ]; then
    if [ -z "${OPENTS_DEV_BUILD:-}" ]; then
        fail "not building release $VERSION: $PROBLEM. (OPENTS_DEV_BUILD=1 makes a $VERSION-dev zip anyway.)"
    fi
    echo "warning: a dev build, not release $VERSION: $PROBLEM" >&2
    NAME=$VERSION-dev-$SHORT${MODIFIED:+-modified}
    EXPECT_RELEASE=0
else
    NAME=$VERSION
    EXPECT_RELEASE=1
fi

# The container sees the repository at the path it has on the host, and the repository a git
# worktree belongs to at its own path, so that the .git files in the worktree and its submodules
# find them and the build can tell what it was made from.
COMMON=$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)
MOUNTS="-v $ROOT:$ROOT"
case "$COMMON" in
    "$ROOT"/*) ;;
    *) MOUNTS="$MOUNTS -v $COMMON:$COMMON" ;;
esac
# shellcheck disable=SC2086
docker run --rm $MOUNTS opents-riscos "$ROOT/port/riscos/build.sh"
# shellcheck disable=SC2086
docker run --rm $MOUNTS opents-riscos "$ROOT/port/riscos/package.sh" >/dev/null

# What the build says it is must be what was asked for.
HEADER=$ROOT/build/riscos/generated/opents_port.h
BUILT_RELEASE=$(sed -n 's/^#define OPENTS_PORT_RELEASE  *//p' "$HEADER")
BUILT_VERSION=$(sed -n 's/^#define OPENTS_PORT_VERSION  *"\(.*\)"$/\1/p' "$HEADER")
if [ "$BUILT_RELEASE" != "$EXPECT_RELEASE" ]; then
    fail "the build says release=$BUILT_RELEASE ($BUILT_VERSION), not $EXPECT_RELEASE; not making a zip"
fi
if [ "$EXPECT_RELEASE" = 1 ] && [ "$BUILT_VERSION" != "$VERSION" ]; then
    fail "the build says $BUILT_VERSION, not $VERSION; not making a zip"
fi
echo "built: OpenTS $(sed -n 's/^#define OPENTS_PORT_VERSION_LINE  *"\(.*\)"$/\1/p' "$HEADER")"

OUT=$ROOT/build/release
mkdir -p "$OUT"
ZIP=$OUT/OpenTS-riscos-$NAME.zip
rm -f "$ZIP"
python3 "$ROOT/port/riscos/tools/riscos-zip.py" "$ZIP" "$ROOT/build/riscos/!OpenTS" >/dev/null
unzip -p "$ZIP" '!OpenTS/!Help' | head -n 1
unzip -l "$ZIP" | tail -1
shasum -a 256 "$ZIP"
