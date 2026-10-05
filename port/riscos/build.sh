#!/bin/sh -e
# Cross-builds OpenTS for RISC OS in the opents-riscos image, with the repository at /work:
#   docker run --rm -v <repo>:/work opents-riscos /work/port/riscos/build.sh
cmake -S /work/port -B /work/build/riscos -G Ninja -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-RelWithDebInfo} \
  -DCMAKE_TOOLCHAIN_FILE=/work/port/riscos/toolchain.cmake
cmake --build /work/build/riscos -j"$(nproc)" "$@"
