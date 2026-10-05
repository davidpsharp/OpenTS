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
| `riscos/` | The RISC OS cross toolchain image. |

Everything is drawn in software, because RISC OS has no 3D drivers: the 565 game frame is
scaled into a 32-bit screen, the UI overlay is rasterised over it, and SDL copies the screen
to the window.

## Running

Put the program and the `ui` directory in a directory holding the Tiberian Sun and Firestorm
mix files (the freeware GDI and Firestorm discs work, unpatched), and run it from there.
On macOS the game waits for its window to gain the focus; these variables help with runs
nobody is watching:

| Variable | Effect |
|---|---|
| `OPENTS_FOCUS=1` | Starts as if the window had the focus. |
| `OPENTS_SHOT=<file.bmp>` | Saves the screen for each of the first 10 frames presented, then every 60th. |

## Known gaps

- Text drawn through GDI fonts (`ownrdraw.cpp`) is not drawn.
- The developer overlay (Dear ImGui) is not drawn by the software renderer.
- Where `long` is 64 bits (macOS), structures that use `long` for on-disk or network data
  are the wrong size; the VQA library, SHA and Base64 have been fixed. RISC OS, like
  Windows, has 32-bit `long`.
