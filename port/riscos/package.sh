#!/bin/sh -e
# Makes build/riscos/!OpenTS from a finished cross build, in the opents-riscos image:
#   docker run --rm -v <repo>:/work opents-riscos /work/port/riscos/package.sh
# The game data is not included: it's installed from the freeware CD images, dragged onto the
# game's icon (Utils.tsprep reads them).
# The repository: where this script is, so a mount at any path works (a git worktree needs its
# repository mounted at the same path as on the host, for the .git file to find it).
W=$(cd "$(dirname "$0")/../.." && pwd)
ENV=/root/gccsdk/env
OUT=$W/build/riscos/!OpenTS
rm -rf "$OUT"
mkdir -p "$OUT"
cp $W/port/riscos/app/!OpenTS/* "$OUT/"
# The version and its date, from the header the build made, at the top of !Help (the Info
# window and the log say the same).
LINE=$(sed -n 's/^#define OPENTS_PORT_VERSION_LINE  "\(.*\)"$/\1/p' $W/build/riscos/generated/opents_port.h)
test -n "$LINE"
RULE=$(printf 'OpenTS %s' "$LINE" | sed 's/./=/g')
sed -i -e "s/@VERSION_LINE@/$LINE/" -e "s/@RULE@/$RULE/" "$OUT/!Help,fff"
! grep -q '@[A-Z_]*@' "$OUT/!Help,fff"
$ENV/bin/arm-riscos-gnueabihf-strip -o $W/build/riscos/OpenTS.stripped $W/build/riscos/OpenTS
$ENV/bin/elf2aif -e $W/build/riscos/OpenTS.stripped "$OUT/!RunImage,ff8" >/dev/null
rm $W/build/riscos/OpenTS.stripped
cp -r $W/ui "$OUT/ui"
# RISC OS: the menus' text in the shipped Arimo font (with a shadow, as the bitmap font has),
# not the game's bitmap font, which the 640x480 menus magnify into blocks on a big screen.
sed -i -e 's/font-family: dlgsys;/font-family: Arimo;/' "$OUT/ui/kit.rcss"
grep -q 'font-family: Arimo;' "$OUT/ui/kit.rcss"
awk '{ print } /font-family: Arimo;/ { arimo = 1 } arimo && /^}/ { printf "\n* \n{\n\tfont-effect: shadow(1dp 1dp #000000);\n}\n"; arimo = 0 }' \
	"$OUT/ui/kit.rcss" > "$OUT/ui/kit.rcss.new" && mv "$OUT/ui/kit.rcss.new" "$OUT/ui/kit.rcss"

# The installer, which reads the CD images.
mkdir -p "$OUT/Utils"
# The build it names in its install log. Like the game's own stamp, it isn't called dirty
# for the SDL submodule, which build.sh patches from the patches kept in this repository.
BUILD=$(git -C $W describe --tags --always 2>/dev/null || date +%Y%m%d)
if [ -n "$(git -C $W status --porcelain --untracked-files=no --ignore-submodules=dirty 2>/dev/null)" ]; then
	BUILD="$BUILD-dirty"
fi
$ENV/bin/arm-riscos-gnueabihf-gcc -O2 -static -DTOOLS_BUILD="\"$BUILD\"" -o $W/build/riscos/tsprep $W/port/riscos/tools/tsprep.c
$ENV/bin/arm-riscos-gnueabihf-strip $W/build/riscos/tsprep
$ENV/bin/elf2aif -e $W/build/riscos/tsprep "$OUT/Utils/tsprep,ff8" >/dev/null

# The licences, as text files.
mkdir -p "$OUT/Licences"
cp $W/LICENSE.md "$OUT/Licences/OpenTS,fff"
cp $W/THIRD_PARTY_NOTICES.md "$OUT/Licences/ThirdParty,fff"
cp $W/ui/OFL.txt "$OUT/Licences/Arimo,fff"
# UnixLib (libunixlib.a, from the riscos-unixlib release in the Dockerfile) is linked into
# !RunImage; its COPYING is kept here because the release has none of its own.
cp $W/port/riscos/licences/UnixLib.txt "$OUT/Licences/UnixLib,fff"
echo "$OUT"
