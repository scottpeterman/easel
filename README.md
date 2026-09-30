# Easel

A balanced layered image editor: Paint.NET approachability with Photoshop's layer model. C++20, Qt 6.

## Status

M1. Paint and erase with a round brush: pen pressure for size and opacity, hardness, flow, spacing and a stabilizer. Opacity caps within a stroke, as in Photoshop, and strokes blend in linear light. Undo and redo keep only the tiles each stroke changed, within a 1 GB budget, and the History panel jumps to any step.

The canvas draws on the GPU through QRhi (Direct3D 11 on Windows, Metal on macOS, OpenGL on Linux) from a sparse tile store (64×64, RGBA16F, linear light), with pan, zoom and rotate. Opening an image runs in the background. Layers, selections and the rest arrive by milestone.

## Controls

| Action | Input |
| --- | --- |
| Paint | Left-drag or pen |
| Brush / Eraser | B / E |
| Smaller / larger brush | [ / ] |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Pan | Middle-drag, or hold Space and drag |
| Zoom at cursor | Mouse wheel |
| Rotate view | Shift + wheel, or Ctrl+[ and Ctrl+] |
| Reset rotation | Ctrl+Shift+R |
| Fit to window | Ctrl+0 |
| Actual pixels | Ctrl+1 |

## Build

Requires CMake 3.21+, Ninja, a C++20 compiler and Qt 6.7+ with the Qt Shader Tools module. CI and releases use Qt 6.10.3.

On Linux and macOS, `scripts/build.sh` configures, builds and runs the tests. It finds Qt at `~/Qt/6.10.3/gcc_64` (Linux) or `~/Qt/6.10.3/macos` by default, and wipes a build directory that was configured from another source tree, another Qt or another generator.

```
scripts/build.sh
scripts/build.sh --run
scripts/build.sh --qt ~/Qt/6.10.2/gcc_64
scripts/build.sh --clean
scripts/build.sh --appimage
scripts/build.sh --dmg
```

`--appimage` writes `dist/Easel-linux-x86_64.AppImage`; `--dmg` writes `dist/Easel-macos.dmg`. CI runs the same script. `--debug` builds into `build-debug/` and `--no-tests` skips ctest.

If Qt reports no Shader Tools module, add it:

```
aqt install-qt linux desktop 6.10.3 linux_gcc_64 --noarchives -m qtshadertools -O ~/Qt
```

Manual build, any platform:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
```

## Releases

Every push to `main` builds and tests on Linux, Windows and macOS and uploads packages as workflow artifacts. Pushing a `v*` tag also publishes a GitHub release:

```
git tag v0.1.0
git push origin v0.1.0
```

| Platform | Package |
| --- | --- |
| Linux x86_64 | `Easel-linux-x86_64.AppImage` |
| Windows x64 | `Easel-windows-x64.zip` (run `bin/easel.exe`) |
| macOS (Apple Silicon + Intel) | `Easel-macos.dmg` |

The macOS build is ad-hoc signed, not notarized. After copying it to Applications, clear the quarantine flag once or macOS reports it as damaged:

```
xattr -dr com.apple.quarantine /Applications/Easel.app
```

## Layout

```
src/core   tile store and color math, no widgets (easel_core)
src/ui     canvas view, main window, panels (easel_ui)
src/app    executable, icon, install and deploy rules
scripts    build.sh: build, test and package on Linux and macOS
tests      Qt Test suites; CPU tests run offscreen, GPU tests need a display
```
