# CMake toolchain for OpenTS on RISC OS 5 (ARMv7, such as the Raspberry Pi 4). C is
# compiled by GCCSDK GCC 10; C++ by clang through port/riscos/clang++, because the engine
# needs __declspec(property); GCC links. Run inside the opents-riscos image (Dockerfile).
set(CMAKE_SYSTEM_NAME GNU)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(RISCOS ON)
set(GCCSDK_ENV /root/gccsdk/env)
set(CMAKE_C_COMPILER ${GCCSDK_ENV}/bin/arm-riscos-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/clang++)
set(CMAKE_AR ${GCCSDK_ENV}/bin/arm-riscos-gnueabihf-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${GCCSDK_ENV}/bin/arm-riscos-gnueabihf-ranlib CACHE FILEPATH "")
set(CMAKE_STRIP ${GCCSDK_ENV}/bin/arm-riscos-gnueabihf-strip CACHE FILEPATH "")
# -fstack-clash-protection: ARMEABISupport maps the stack a page at a time, so a frame
# larger than a page must probe it. clang ignores this flag on 32-bit ARM, which is why
# the program also maps its stacks in advance (port/riscos/stack.cpp).
set(CMAKE_C_FLAGS_INIT "-mtune=cortex-a72 -mfpu=vfpv3 -mfloat-abi=hard -fstack-clash-protection -mno-unaligned-access")
# clang only compiles; GCC's driver links against UnixLib and libstdc++.
set(CMAKE_CXX_LINK_EXECUTABLE "${GCCSDK_ENV}/bin/arm-riscos-gnueabihf-g++ <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(CMAKE_FIND_ROOT_PATH ${GCCSDK_ENV})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(BUILD_SHARED_LIBS OFF)
