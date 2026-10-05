# The POSIX build of OpenTS, for macOS and RISC OS. It builds the engine from code/ with the
# Win32 stand-ins in port/compat and the software presenter, UI renderer, audio output and
# debug log in port/posix. Configure it as its own project:
#
#   cmake -S port -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
#
# The compiler must be clang: the engine uses MSVC's __declspec(property), which GCC lacks.

cmake_minimum_required(VERSION 3.23)
project(OpenTSPosix VERSION 0.2.0 LANGUAGES C CXX)

set(OTS "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The POSIX build needs clang for __declspec(property).")
endif()

option(OPENTS_ASAN "Build with AddressSanitizer" OFF)

#
# Generated headers: the build stamp and the string names, as the main build makes them,
# plus the string table that stands in for the language DLL.
#
set(GENERATED "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED}")
execute_process(COMMAND ${CMAKE_COMMAND}
    -DOPENTS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    -DOPENTS_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    -DOPENTS_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    -DOPENTS_VERSION_PRERELEASE=
    -DOPENTS_OFFICIAL_BUILD=OFF
    -DOPENTS_SOURCE_DIR=${OTS}
    -DOPENTS_VERSION_HEADER=${GENERATED}/opents_version.h
    -DOPENTS_STAMP_HEADER=${GENERATED}/opents_build.h
    -DOPENTS_ARCH=${CMAKE_SYSTEM_PROCESSOR}
    -P "${OTS}/cmake/GitStamp.cmake")
execute_process(COMMAND ${CMAKE_COMMAND}
    -DOPENTS_LANGUAGE_HEADER=${OTS}/code/language/language.h
    -DOPENTS_STRINGS_HEADER=${GENERATED}/opents_strings.h
    -P "${OTS}/cmake/StringTable.cmake")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
    OUTPUT "${GENERATED}/rcstrings.cpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/rcstrings.py"
        "${OTS}/code/language/language.h" "${OTS}/code/language/language.rc" "${GENERATED}/rcstrings.cpp"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/rcstrings.py" "${OTS}/code/language/language.rc" "${OTS}/code/language/language.h"
    COMMENT "Generating the string table")

#
# Third-party libraries
#
set(TP "${OTS}/thirdparty")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_AUDIO ON CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
foreach(subsystem GPU CAMERA JOYSTICK HAPTIC HIDAPI POWER SENSOR DIALOG TRAY)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
# macOS gives SDL no window framebuffer of its own, so SDL makes one with a Metal renderer.
if(APPLE)
    set(SDL_METAL ON CACHE BOOL "" FORCE)
else()
    set(SDL_METAL OFF CACHE BOOL "" FORCE)
endif()
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/SDL" SDL EXCLUDE_FROM_ALL)

file(GLOB LZO_SOURCES "${TP}/lzo/src/*.c")
add_library(lzo STATIC ${LZO_SOURCES})
target_include_directories(lzo PUBLIC "${TP}/lzo/include" PRIVATE "${TP}/lzo/src")

add_library(miniaudio STATIC "${TP}/miniaudio-impl.c")
target_include_directories(miniaudio PUBLIC "${TP}/miniaudio")
target_compile_definitions(miniaudio PUBLIC
    MA_NO_DEVICE_IO MA_NO_ENGINE MA_NO_NODE_GRAPH MA_NO_RESOURCE_MANAGER MA_NO_GENERATION MA_NO_ENCODING)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory("${TP}/freetype" freetype EXCLUDE_FROM_ALL)
add_library(Freetype::Freetype ALIAS freetype)

set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_TRACY_PROFILING OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Freetype ON)
add_subdirectory("${TP}/RmlUi" RmlUi EXCLUDE_FROM_ALL)

add_library(imgui STATIC
    "${TP}/imgui/imgui.cpp"
    "${TP}/imgui/imgui_demo.cpp"
    "${TP}/imgui/imgui_draw.cpp"
    "${TP}/imgui/imgui_tables.cpp"
    "${TP}/imgui/imgui_widgets.cpp")
target_include_directories(imgui PUBLIC "${TP}/imgui")
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

#
# The engine
#
file(GLOB_RECURSE ENGINE_SOURCES "${OTS}/code/*.cpp" "${OTS}/code/*.c")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/code/language/")
# Replaced by the files in port/posix.
list(REMOVE_ITEM ENGINE_SOURCES
    "${OTS}/code/dbgprint.cpp"
    "${OTS}/code/bgfxbackend.cpp"
    "${OTS}/code/ui/rml/rmlrender.cpp"
    "${OTS}/code/audio/audiodevice_ma.cpp")

file(GLOB PORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/posix/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/compat/*.cpp")

add_executable(OpenTS ${ENGINE_SOURCES} ${PORT_SOURCES} "${GENERATED}/rcstrings.cpp")
target_include_directories(OpenTS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/posix"
    "${OTS}/code"
    "${OTS}/code/vqalib"
    "${GENERATED}"
    "${TP}/bgfx.cmake/bimg/3rdparty/tinyexr/deps"
    "${TP}/bgfx.cmake/bimg/3rdparty/stb")
target_compile_options(OpenTS PRIVATE
    -fsigned-char
    -fno-strict-aliasing
    -fwrapv
    -Wno-everything
    "$<$<COMPILE_LANGUAGE:CXX>:-fms-extensions;-fdeclspec;SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/compat/msvcrt.h>")
# miniz for writepng.cpp comes from bimg, which this build does not otherwise use.
target_sources(OpenTS PRIVATE "${TP}/bgfx.cmake/bimg/3rdparty/tinyexr/deps/miniz/miniz.c")
target_link_libraries(OpenTS PRIVATE lzo miniaudio RmlUi::Core imgui SDL3::SDL3-static)

if(OPENTS_ASAN)
    target_compile_options(OpenTS PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(OpenTS PRIVATE -fsanitize=address,undefined)
endif()

