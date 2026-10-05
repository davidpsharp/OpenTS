# The POSIX build of OpenTS

This directory builds OpenTS for macOS and for RISC OS 5 on ARMv7 machines such as the
Raspberry Pi 4. It is a separate CMake project; the Windows build is unchanged.

```bash
cmake -S port -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build/mac
```

The compiler must be clang: the engine uses MSVC's `__declspec(property)`, which GCC lacks.

| Directory | Contents |
|---|---|
| `compat/` | Win32 declarations (`windows.h` and friends) and their POSIX implementations (`win32.cpp`). GDI, registry and window-message calls report failure or do nothing. |
| `posix/` | Replacements for the Windows-only engine files: the software frame presenter (`softbackend.cpp`, for `bgfxbackend.cpp`), the software UI renderer (`rmlrendersoft.cpp`, for the bgfx RmlUi renderer), SDL audio output (`audiodevice_sdl.cpp`) and the debug log (`dbgprint.cpp`). |
| `tools/rcstrings.py` | Turns `code/language/language.rc` into the string table that `LoadString` reads, in place of `Language.dll`. |
| `riscos/` | The RISC OS cross toolchain (a Docker image), the application directory, the stack and crash-report code, and headers GCC 10's library lacks. |

Everything is drawn in software, because RISC OS has no 3D drivers: the 565 game frame is
scaled into a 32-bit screen, the UI overlay is rasterised over it, and SDL copies the screen
to the window.

## Building for RISC OS

The cross build runs in a Docker image made from `riscos/Dockerfile`, which adds clang to an
image named `riscos-gcc10` holding GCCSDK's GCC 10 and UnixLib in `/root/gccsdk`. clang
compiles the C++, GCC the C libraries, and GCC links.

```bash
docker build -t opents-riscos port/riscos
docker run --rm -v "$PWD":/work opents-riscos /work/port/riscos/build.sh
docker run --rm -v "$PWD":/work opents-riscos /work/port/riscos/package.sh
```

`package.sh` makes `build/riscos/!OpenTS`, with the program converted to AIF and the `ui`
directory. The game data goes in it too, with RISC OS names (`TIBSUN/MIX`). `!Run` loads SharedUnixLibrary,
ARMEABISupport and DigitalRenderer, and passes its arguments on, so `-SPAWN` works.

RISC OS on ARMv7 faults on unaligned loads and stores, which Westwood's data formats invite.
`-DOPENTS_ALIGNCHECK=ON` builds a Mac binary with the alignment sanitizer that reports
each one, and a `SPAWN.INI` like this one starts a skirmish without the menus (`-SPAWN`):

```ini
[Settings]
Name=Tester
Scenario=G_CANYON.MAP
Side=0
Color=0
GameSpeed=0
AIPlayers=1
Bases=yes
[HouseCountries]
Multi2=1
[HouseColors]
Multi2=1
```

The debug log is in `Debug` beside the program. On RISC OS it is closed after each line, so
it can be read while the game runs, and a crash adds the registers and the return addresses
on the stack to `stderr`; give those to `addr2line` with the unstripped ELF.

## Running

Put the program and the `ui` directory in a directory holding the Tiberian Sun and Firestorm
mix files (the freeware GDI and Firestorm discs work, unpatched), and run it from there.
On macOS the game waits for its window to gain the focus; these variables help with runs
nobody is watching:

| Variable | Effect |
|---|---|
| `OPENTS_FOCUS=1` | Starts as if the window had the focus. |
| `OPENTS_SHOT=<file.bmp>` | Saves the screen for each of the first 10 frames presented, then every 60th. |
| `OPENTS_FPS=1` | Logs the presented and game frame rates every five seconds. |
| `OPENTS_SYNCLOG=1` | Logs a summary of the game state every 500 frames. The same `SPAWN.INI` game gives the same lines on the Mac and the Pi, so a difference shows where two machines part. |

On RISC OS these are system variables (`*Set OPENTS_FPS 1`).

On a Raspberry Pi 4 a 640x480 skirmish runs at 60 game frames a second, the game's fastest
speed.

## Known gaps

- Text drawn through GDI fonts (`ownrdraw.cpp`) is not drawn; only the end credits use it.
- The developer overlay (Dear ImGui) is not drawn by the software renderer.
- Where `long` is 64 bits (macOS), structures that use `long` for on-disk or network data
  are the wrong size; the VQA library, SHA and Base64 have been fixed. RISC OS, like
  Windows, has 32-bit `long`.
