#!/bin/sh -e
# Makes build/riscos/!OpenTS from a finished cross build, in the opents-riscos image:
#   docker run --rm -v <repo>:/work opents-riscos /work/port/riscos/package.sh
# The game data is not included: it's installed from the freeware CD images, dragged onto the
# game's icon (Utils.tsprep reads them).
ENV=/root/gccsdk/env
OUT=/work/build/riscos/!OpenTS
rm -rf "$OUT"
mkdir -p "$OUT"
cp /work/port/riscos/app/!OpenTS/* "$OUT/"
$ENV/bin/arm-riscos-gnueabihf-strip -o /work/build/riscos/OpenTS.stripped /work/build/riscos/OpenTS
$ENV/bin/elf2aif -e /work/build/riscos/OpenTS.stripped "$OUT/!RunImage,ff8" >/dev/null
rm /work/build/riscos/OpenTS.stripped
cp -r /work/ui "$OUT/ui"
# RISC OS: the menus' text in the shipped Arimo font (with a shadow, as the bitmap font has),
# not the game's bitmap font, which the 640x480 menus magnify into blocks on a big screen.
sed -i -e 's/font-family: dlgsys;/font-family: Arimo;/' "$OUT/ui/kit.rcss"
grep -q 'font-family: Arimo;' "$OUT/ui/kit.rcss"
awk '{ print } /font-family: Arimo;/ { arimo = 1 } arimo && /^}/ { printf "\n* \n{\n\tfont-effect: shadow(1dp 1dp #000000);\n}\n"; arimo = 0 }' \
	"$OUT/ui/kit.rcss" > "$OUT/ui/kit.rcss.new" && mv "$OUT/ui/kit.rcss.new" "$OUT/ui/kit.rcss"

# The installer, which reads the CD images.
mkdir -p "$OUT/Utils"
# The build it names in its install log.
BUILD=$(git -C /work describe --tags --always --dirty 2>/dev/null || date +%Y%m%d)
$ENV/bin/arm-riscos-gnueabihf-gcc -O2 -static -DTOOLS_BUILD="\"$BUILD\"" -o /work/build/riscos/tsprep /work/port/riscos/tools/tsprep.c
$ENV/bin/arm-riscos-gnueabihf-strip /work/build/riscos/tsprep
$ENV/bin/elf2aif -e /work/build/riscos/tsprep "$OUT/Utils/tsprep,ff8" >/dev/null

# The licences, as text files.
mkdir -p "$OUT/Licences"
cp /work/LICENSE.md "$OUT/Licences/OpenTS,fff"
cp /work/THIRD_PARTY_NOTICES.md "$OUT/Licences/ThirdParty,fff"
cp /work/ui/OFL.txt "$OUT/Licences/Arimo,fff"
# UnixLib (libunixlib.a, from the riscos-unixlib release in the Dockerfile) is linked into
# !RunImage; its COPYING is kept here because the release has none of its own.
cp /work/port/riscos/licences/UnixLib.txt "$OUT/Licences/UnixLib,fff"
echo "$OUT"
