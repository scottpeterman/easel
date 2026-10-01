# Easel

A balanced layered image editor: Paint.NET approachability with Photoshop's layer model. C++20, Qt 6.

## Status

M1. Paint and erase with a round brush: pen pressure for size and opacity, hardness, flow, spacing and a stabilizer. Opacity caps within a stroke, as in Photoshop, and strokes blend in linear light. Undo and redo keep only the tiles each stroke changed, within a 1 GB budget, and the History panel jumps to any step. The Color panel has a hue ring with a saturation/value square, hex entry, recent colours and a saved palette; the eyedropper shows a before/after ring while you pick.

The canvas draws on the GPU through QRhi (Direct3D 11 on Windows, Metal on macOS, OpenGL on Linux) from a sparse tile store (64×64, RGBA16F, linear light), with pan, zoom and rotate. Opening an image runs in the background. Layers, selections and the rest arrive by milestone.

## Controls

| Action | Input |
| --- | --- |
| New / Open | Ctrl+N / Ctrl+O (Easel documents and images) |
| Save / Save As | Ctrl+S / Ctrl+Shift+S |
| Export PNG, JPEG or WebP | Ctrl+Shift+E |
| Paint | Left-drag or pen |
| Brush / Eraser / Smudge / Eyedropper | B / E / S / I |
| Hard 1 px pixels (sprites) | Tick **Pixel** in the tool options |
| Rectangle / ellipse select | M / Shift+M; drag, Shift for square or circle, click to deselect |
| Select all / deselect | Ctrl+A / Ctrl+D |
| Cut / copy / paste / delete | Ctrl+X / Ctrl+C / Ctrl+V / Delete |
| Move selected pixels | V, then drag; arrows nudge 1 px, Shift+arrows 10 px |
| Drop / cancel floating pixels | Enter / Escape |
| Magic wand | W; click selects similar colour, Shift+click adds, Ctrl+click subtracts |
| Invert selection | Ctrl+Shift+I |
| Grow / shrink selection | Edit > Grow Selection, Shrink Selection |
| Crop to selection | Ctrl+Shift+X |
| Trim transparent edges | Ctrl+Alt+T |
| Color to Alpha | Ctrl+Alt+A (in the selection, or the whole canvas) |
| Export selection as PNG | Ctrl+Alt+E |
| Pixel grid (from 600%) | Ctrl+' |
| Sprite grid on / off | Ctrl+Shift+' (cell size, offset and snapping under View > Sprite Grid Settings) |
| Pick a colour from any tool | Hold Alt and click or drag |
| Smaller / larger brush | [ / ] |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Pan | Middle-drag, or hold Space and drag |
| Zoom at cursor | Mouse wheel (whole-pixel steps above 100%: 200%, 300%, 400%...) |
| Rotate view | Shift + wheel, or Ctrl+[ and Ctrl+] |
| Reset rotation | Ctrl+Shift+R |
| Fit to window | Ctrl+0 |
| Actual pixels | Ctrl+1 |

## Files

Easel saves `.easel` documents: a zip with a `manifest.json`, a flattened `preview.png` (up to 2048 px) you can look at without Easel, and the canvas tiles stored exactly (16-bit float, linear light), so saving and reopening never loses quality. Areas you haven't painted take no space. Saves and exports run in the background, and a failed save never damages the previous file. Export writes a full-size flattened PNG, JPEG or WebP.

## Build

Requires CMake 3.21+, Ninja, a C++20 compiler and Qt 6.7+ with the Qt Shader Tools module. CI and releases use Qt 6.10.3.

On Linux and macOS, `scripts/build.sh` configures, builds and runs the tests. On Windows, `scripts\build-windows.bat` does the same (see below). It finds Qt at `~/Qt/6.10.3/gcc_64` (Linux) or `~/Qt/6.10.3/macos` by default, and wipes a build directory that was configured from another source tree, another Qt or another generator.

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

On Windows, `scripts\build-windows.bat` takes the same options, with `--zip` (writes `dist\Easel-windows-x64.zip`) in place of `--appimage`/`--dmg`. It runs from a plain cmd prompt: it loads the x64 MSVC environment itself through vswhere, finds Qt at `C:\Qt\6.10.3\msvc2022_64` (or the newest `C:\Qt\6.*\msvc*_64`), and takes Ninja and CMake from PATH, Visual Studio or `C:\Qt\Tools`. Needs Visual Studio 2022 or its Build Tools with "Desktop development with C++".

```
scripts\build-windows.bat
scripts\build-windows.bat --run
scripts\build-windows.bat --qt C:\Qt\6.10.3\msvc2022_64
scripts\build-windows.bat --zip
```

If your Qt came over from another project without Shader Tools, add it with the Qt Maintenance Tool (Additional Libraries > Qt Shader Tools) or:

```
aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 --noarchives -m qtshadertools -O C:\Qt
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
scripts    build.sh (Linux, macOS) and build-windows.bat: build, test and package
tests      Qt Test suites; CPU tests run offscreen, GPU tests need a display
```
