#!/bin/sh -e
# Makes build/riscos/!OpenTS from a finished cross build, in the opents-riscos image:
#   docker run --rm -v <repo>:/work opents-riscos /work/port/riscos/package.sh
# The game data is not included; copy the mix files into !OpenTS with RISC OS names.
ENV=/root/gccsdk/env
OUT=/work/build/riscos/!OpenTS
rm -rf "$OUT"
mkdir -p "$OUT"
cp "/work/port/riscos/app/!OpenTS/!Run,feb" "$OUT/"
$ENV/bin/arm-riscos-gnueabihf-strip -o /work/build/riscos/OpenTS.stripped /work/build/riscos/OpenTS
$ENV/bin/elf2aif -e /work/build/riscos/OpenTS.stripped "$OUT/!RunImage,ff8" >/dev/null
rm /work/build/riscos/OpenTS.stripped
cp -r /work/ui "$OUT/ui"
echo "$OUT"
