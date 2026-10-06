#!/bin/sh -e
# Makes build/riscos/!OpenTS from a finished cross build, in the opents-riscos image:
#   docker run --rm -v <repo>:/work opents-riscos /work/port/riscos/package.sh
# The game data is not included: Prepare, which goes beside !OpenTS, installs it from the
# freeware CD images.
ENV=/root/gccsdk/env
OUT=/work/build/riscos/!OpenTS
rm -rf "$OUT"
mkdir -p "$OUT"
cp /work/port/riscos/app/!OpenTS/* "$OUT/"
$ENV/bin/arm-riscos-gnueabihf-strip -o /work/build/riscos/OpenTS.stripped /work/build/riscos/OpenTS
$ENV/bin/elf2aif -e /work/build/riscos/OpenTS.stripped "$OUT/!RunImage,ff8" >/dev/null
rm /work/build/riscos/OpenTS.stripped
cp -r /work/ui "$OUT/ui"

# Prepare's helper, which reads the CD images.
mkdir -p "$OUT/Utils"
$ENV/bin/arm-riscos-gnueabihf-gcc -O2 -static -o /work/build/riscos/tsprep /work/port/riscos/tools/tsprep.c
$ENV/bin/arm-riscos-gnueabihf-strip /work/build/riscos/tsprep
$ENV/bin/elf2aif -e /work/build/riscos/tsprep "$OUT/Utils/tsprep,ff8" >/dev/null
cp /work/port/riscos/app/Prepare,feb /work/build/riscos/

# The licences, as text files.
mkdir -p "$OUT/Licences"
cp /work/LICENSE.md "$OUT/Licences/OpenTS,fff"
cp /work/THIRD_PARTY_NOTICES.md "$OUT/Licences/ThirdParty,fff"
cp /work/ui/OFL.txt "$OUT/Licences/Arimo,fff"
echo "$OUT"
