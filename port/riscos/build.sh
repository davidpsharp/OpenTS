#!/bin/sh -e
# Cross-builds OpenTS for RISC OS in the opents-riscos image, with the repository at /work:
#   docker run --rm -v <repo>:/work opents-riscos /work/port/riscos/build.sh
# RISC OS fixes to SDL's RISC OS driver, kept here as patches (thirdparty/SDL is upstream's
# submodule); each is applied once, so building again leaves it as it is.
for p in /work/port/riscos/patches/sdl-*.patch; do
  if ! git -C /work/thirdparty/SDL apply --reverse --check "$p" 2>/dev/null; then
    git -C /work/thirdparty/SDL apply "$p"
  fi
done
cmake -S /work/port -B /work/build/riscos -G Ninja -DOPENTS_SUBMODULES_PATCHED=ON -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-RelWithDebInfo} \
  -DCMAKE_TOOLCHAIN_FILE=/work/port/riscos/toolchain.cmake
cmake --build /work/build/riscos -j"$(nproc)" "$@"
