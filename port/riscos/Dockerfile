# The RISC OS cross toolchain for OpenTS: GCCSDK GCC 10.2 (arm-riscos-gnueabihf) supplies the
# headers, libraries and linker; clang compiles, because the engine uses MSVC's
# __declspec(property), which GCC lacks. Build with:
#   docker build -t opents-riscos port/riscos
FROM riscos-gcc10:latest
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        clang-18 lld-18 ninja-build python3 && \
    rm -rf /var/lib/apt/lists/*
