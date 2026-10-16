# The RISC OS cross toolchain for OpenTS: GCCSDK GCC 10.2 (arm-riscos-gnueabihf) supplies the
# headers, libraries and linker; clang compiles, because the engine uses MSVC's
# __declspec(property), which GCC lacks. Build with:
#   docker build -t opents-riscos port/riscos
FROM riscos-gcc10:latest
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        clang-18 lld-18 ninja-build python3 && \
    rm -rf /var/lib/apt/lists/*

# UnixLib 5.0.3.3 from adyoull's riscos-unixlib (an unofficial fork of GCCSDK's UnixLib, which
# riscos-gcc10's is already a build of): it adds, for OpenTS, a heap that grows past a gap after
# it (A3) and a /dev/dsp write on DigitalRenderer that gives up after 2 s.
# See https://github.com/adyoull/riscos-unixlib/blob/main/CHANGELOG.md.
ADD --checksum=sha256:e24f16c3f4612aae3871ceff521ab86b0d3ac4cc689d734e0e4085d02c774adf \
    https://github.com/adyoull/riscos-unixlib/releases/download/v5.0.3.3/libunixlib.a \
    /root/gccsdk/env/arm-riscos-gnueabihf/lib/libunixlib.a
